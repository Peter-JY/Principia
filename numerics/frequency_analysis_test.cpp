﻿
#include "numerics/frequency_analysis.hpp"

#include <algorithm>
#include <functional>
#include <random>
#include <vector>

#include "geometry/frame.hpp"
#include "geometry/grassmann.hpp"
#include "geometry/hilbert.hpp"
#include "geometry/named_quantities.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "numerics/apodization.hpp"
#include "numerics/fast_fourier_transform.hpp"
#include "numerics/polynomial_evaluators.hpp"
#include "quantities/elementary_functions.hpp"
#include "quantities/named_quantities.hpp"
#include "quantities/quantities.hpp"
#include "quantities/si.hpp"
#include "testing_utilities/almost_equals.hpp"
#include "testing_utilities/approximate_quantity.hpp"
#include "testing_utilities/is_near.hpp"
#include "testing_utilities/numerics_matchers.hpp"

namespace principia {
namespace numerics {
namespace frequency_analysis {

using geometry::Displacement;
using geometry::Frame;
using geometry::Handedness;
using geometry::Hilbert;
using geometry::Inertial;
using geometry::Instant;
using geometry::Vector;
using quantities::Abs;
using quantities::Acceleration;
using quantities::AngularFrequency;
using quantities::Jerk;
using quantities::Length;
using quantities::Pow;
using quantities::Product;
using quantities::Sin;
using quantities::Snap;
using quantities::Speed;
using quantities::Square;
using quantities::Time;
using quantities::si::Metre;
using quantities::si::Radian;
using quantities::si::Second;
using testing_utilities::AlmostEquals;
using testing_utilities::IsNear;
using testing_utilities::RelativeErrorFrom;
using testing_utilities::operator""_⑴;
using ::testing::AllOf;
using ::testing::Gt;
using ::testing::Lt;
namespace si = quantities::si;

class DotImplementation {
 public:
  DotImplementation(Instant const& t_min, Instant const& t_max);

  template<typename LFunction, typename RFunction, typename Weight>
  typename Hilbert<std::invoke_result_t<LFunction, Instant>,
                   std::invoke_result_t<RFunction, Instant>>::InnerProductType
  operator()(LFunction const& left,
             RFunction const& right,
             Weight const& weight) const;

 private:
  Instant const t_min_;
  Instant const t_max_;
};

DotImplementation::DotImplementation(Instant const& t_min,
                                     Instant const& t_max)
    : t_min_(t_min),
      t_max_(t_max) {}

template<typename LFunction, typename RFunction, typename Weight>
typename Hilbert<std::invoke_result_t<LFunction, Instant>,
                 std::invoke_result_t<RFunction, Instant>>::InnerProductType
DotImplementation::operator()(LFunction const& left,
                              RFunction const& right,
                              Weight const& weight) const {
  // We need to use a large number of points otherwise the test
  // PiecewisePoissonSeriesProjection doesn't yield a reasonable solution.  This
  // doesn't happen with real life functions, which are much smoother than the
  // test functions.
  return InnerProduct<std::invoke_result_t<LFunction, Instant>,
                      std::invoke_result_t<RFunction, Instant>,
                      LFunction::degree,
                      RFunction::degree,
                      Weight::degree,
                      HornerEvaluator,
                      /*points=*/30>(left, right, weight, t_min_, t_max_);
}

class FrequencyAnalysisTest : public ::testing::Test {
 protected:
  using World = Frame<serialization::Frame::TestTag,
                      Inertial,
                      Handedness::Right,
                      serialization::Frame::TEST>;

  using Series0 = PoissonSeries<Length, 0, HornerEvaluator>;
  using Series4 = PoissonSeries<Length, 4, HornerEvaluator>;

  FrequencyAnalysisTest()
      : random_polynomial4_([](Instant const& t0,
                               std::mt19937_64& random,
                               std::uniform_real_distribution<>& distribution) {
          auto const c0 = distribution(random) * Metre;
          auto const c1 = distribution(random) * Metre / Second;
          auto const c2 = distribution(random) * Metre / Pow<2>(Second);
          auto const c3 = distribution(random) * Metre / Pow<3>(Second);
          auto const c4 = distribution(random) * Metre / Pow<4>(Second);

          return Series4::Polynomial({c0, c1, c2, c3, c4}, t0);
        }) {}

  Instant const t0_;
  std::function<Series4::Polynomial(
      Instant const& t0,
      std::mt19937_64& random,
      std::uniform_real_distribution<>& distribution)>
      random_polynomial4_;
};

TEST_F(FrequencyAnalysisTest, PreciseModeScalar) {
  using FFT = FastFourierTransform<Length, 1 << 16>;
  AngularFrequency const ω = 666.543 * π / FFT::size * Radian / Second;
  Time const Δt = 1 * Second;
  std::mt19937_64 random(42);
  std::uniform_real_distribution<> amplitude_distribution(-0.1, 0.1);
  std::uniform_real_distribution<> frequency_distribution(-100.0, 100.0);

  Series0::PolynomialsByAngularFrequency polynomials;

  // Main harmonic.
  polynomials.emplace_back(
      ω,
      Series0::Polynomials{/*sin=*/Series0::Polynomial({1 * Metre}, t0_),
                           /*cos=*/Series0::Polynomial({0 * Metre}, t0_)});

  // Noise with lower amplitude and higher frequency.
  for (int i = 0; i < 10; ++i) {
    auto const sin_amplitude = amplitude_distribution(random) * Metre;
    auto const cos_amplitude = amplitude_distribution(random) * Metre;
    polynomials.emplace_back(
        ω * frequency_distribution(random),
        Series0::Polynomials{
            /*sin=*/Series0::Polynomial({sin_amplitude}, t0_),
            /*cos=*/Series0::Polynomial({cos_amplitude}, t0_)});
  }
  Series0 const sin(
      Series0::Polynomial({amplitude_distribution(random) * Metre}, t0_),
      polynomials);

  Instant const t_min = t0_;
  Instant const t_max = t0_ + (FFT::size - 1) * Δt;
  std::vector<Length> signal;
  for (int n = 0; n < FFT::size; ++n) {
    signal.push_back(sin(t0_ + n * Δt));
  }

  // Won't fit on the stack.
  auto transform = std::make_unique<FFT>(signal, Δt);

  // The FFT gives us an accuracy which is of the order of the number of points.
  auto const mode = transform->Mode();
  EXPECT_THAT(mode.midpoint(), RelativeErrorFrom(ω, IsNear(8.1e-4_⑴)));

  DotImplementation dot(t_min, t_max);

  // The precise analysis is only limited by our ability to pinpoint the
  // maximum.
  auto const precise_mode = PreciseMode(
      mode, sin, apodization::Hann<HornerEvaluator>(t_min, t_max), dot);
  EXPECT_THAT(precise_mode, RelativeErrorFrom(ω, IsNear(4.7e-11_⑴)));
}

TEST_F(FrequencyAnalysisTest, PreciseModeVector) {
  using FFT = FastFourierTransform<Displacement<World>, 1 << 16>;
  AngularFrequency const ω = 666.543 * π / FFT::size * Radian / Second;
  Time const Δt = 1 * Second;

  using Series0 = PoissonSeries<Displacement<World>, 0, HornerEvaluator>;
  Series0::PolynomialsByAngularFrequency polynomials;

  // Main harmonic.
  polynomials.emplace_back(
      ω,
      Series0::Polynomials{
          /*sin=*/Series0::Polynomial(
              {Displacement<World>({1 * Metre, 2 * Metre, 3 * Metre})}, t0_),
          /*cos=*/Series0::Polynomial(
              {Displacement<World>({-5 * Metre, 7 * Metre, 11 * Metre})},
              t0_)});
  Series0 const sin(Series0::Polynomial(Displacement<World>(), t0_),
                    polynomials);

  Instant const t_min = t0_;
  Instant const t_max = t0_ + (FFT::size - 1) * Δt;
  std::vector<Displacement<World>> signal;
  for (int n = 0; n < FFT::size; ++n) {
    signal.push_back(sin(t0_ + n * Δt));
  }

  // Won't fit on the stack.
  auto transform = std::make_unique<FFT>(signal, Δt);

  // The FFT gives us an accuracy which is of the order of the number of points.
  auto const mode = transform->Mode();
  EXPECT_THAT(mode.midpoint(), RelativeErrorFrom(ω, IsNear(8.1e-4_⑴)));

  DotImplementation dot(t_min, t_max);

  // The precise analysis is only limited by our ability to pinpoint the
  // maximum.
  auto const precise_mode = PreciseMode(
      mode, sin, apodization::Hann<HornerEvaluator>(t_min, t_max), dot);
  EXPECT_THAT(precise_mode, RelativeErrorFrom(ω, IsNear(4.0e-11_⑴)));
}

TEST_F(FrequencyAnalysisTest, PoissonSeriesScalarProjection) {
  AngularFrequency const ω = 666.543 * π * Radian / Second;
  std::mt19937_64 random(42);
  std::uniform_real_distribution<> amplitude_distribution(-10.0, 10.0);

  auto const sin = random_polynomial4_(t0_, random, amplitude_distribution);
  auto const cos = random_polynomial4_(t0_, random, amplitude_distribution);
  Series4 const series(
      Series4::Polynomial(Series4::Polynomial::Coefficients{}, t0_),
      {{ω, Series4::Polynomials{sin, cos}}});

  Instant const t_min = t0_;
  Instant const t_max = t0_ + 100 * Radian / ω;
  DotImplementation const dot(t_min, t_max);

  // Projection on a 4th degree basis accurately reconstructs the function.
  auto const projection4 = Projection<4>(
      ω, series, apodization::Hann<HornerEvaluator>(t_min, t_max), dot);
  for (int i = 0; i <= 100; ++i) {
    EXPECT_THAT(projection4(t0_ + i * Radian / ω),
                AlmostEquals(series(t0_ + i * Radian / ω), 0, 2688));
  }

  // Projection on a 5th degree basis is also accurate.
  auto const projection5 = Projection<5>(
      ω, series, apodization::Hann<HornerEvaluator>(t_min, t_max), dot);
  for (int i = 0; i <= 100; ++i) {
    EXPECT_THAT(projection5(t0_ + i * Radian / ω),
                AlmostEquals(series(t0_ + i * Radian / ω), 0, 8000));
  }

  // Projection on a 3rd degree basis introduces significant errors.
  auto const projection3 = Projection<3>(
      ω, series, apodization::Hann<HornerEvaluator>(t_min, t_max), dot);
  for (int i = 0; i <= 100; ++i) {
    EXPECT_THAT(projection3(t0_ + i * Radian / ω),
                RelativeErrorFrom(series(t0_ + i * Radian / ω),
                                  AllOf(Gt(3.6e-13), Lt(9.0e-6))));
  }
}

TEST_F(FrequencyAnalysisTest, PoissonSeriesVectorProjection) {
  AngularFrequency const ω = 666.543 * π * Radian / Second;
  std::mt19937_64 random(42);
  std::uniform_real_distribution<> amplitude_distribution(-10.0, 10.0);
  using VectorSeries4 =
      PoissonSeries<Vector<Length, World>, 4, HornerEvaluator>;

  auto random_polynomial4 = [](Instant const& t0,
                               std::mt19937_64& random,
                               std::uniform_real_distribution<>& distribution) {
    auto const c0x = distribution(random) * Metre;
    auto const c1x = distribution(random) * Metre / Second;
    auto const c2x = distribution(random) * Metre / Pow<2>(Second);
    auto const c3x = distribution(random) * Metre / Pow<3>(Second);
    auto const c4x = distribution(random) * Metre / Pow<4>(Second);
    auto const c0y = distribution(random) * Metre;
    auto const c1y = distribution(random) * Metre / Second;
    auto const c2y = distribution(random) * Metre / Pow<2>(Second);
    auto const c3y = distribution(random) * Metre / Pow<3>(Second);
    auto const c4y = distribution(random) * Metre / Pow<4>(Second);
    auto const c0z = distribution(random) * Metre;
    auto const c1z = distribution(random) * Metre / Second;
    auto const c2z = distribution(random) * Metre / Pow<2>(Second);
    auto const c3z = distribution(random) * Metre / Pow<3>(Second);
    auto const c4z = distribution(random) * Metre / Pow<4>(Second);
    Vector<Length, World> const v0({c0x, c0y, c0z});
    Vector<Speed, World> const v1({c1x, c1y, c1z});
    Vector<Acceleration, World> const v2({c2x, c2y, c2z});
    Vector<Jerk, World> const v3({c3x, c3y, c3z});
    Vector<Snap, World> const v4({c4x, c4y, c4z});

    return VectorSeries4::Polynomial({v0, v1, v2, v3, v4}, t0);
  };

  auto const sin = random_polynomial4(t0_, random, amplitude_distribution);
  auto const cos = random_polynomial4(t0_, random, amplitude_distribution);
  VectorSeries4 const series(
      VectorSeries4::Polynomial(VectorSeries4::Polynomial::Coefficients{}, t0_),
      {{ω, VectorSeries4::Polynomials{sin, cos}}});

  Instant const t_min = t0_;
  Instant const t_max = t0_ + 100 * Radian / ω;
  DotImplementation const dot(t_min, t_max);

  // Projection on a 4th degree basis accurately reconstructs the function.
  auto const projection4 = Projection<4>(
      ω, series, apodization::Hann<HornerEvaluator>(t_min, t_max), dot);
  for (int i = 0; i <= 100; ++i) {
    EXPECT_THAT(projection4(t0_ + i * Radian / ω),
                AlmostEquals(series(t0_ + i * Radian / ω), 0, 4016));
  }

  // Projection on a 5th degree basis is also accurate.
  auto const projection5 = Projection<5>(
      ω, series, apodization::Hann<HornerEvaluator>(t_min, t_max), dot);
  for (int i = 0; i <= 100; ++i) {
    EXPECT_THAT(projection5(t0_ + i * Radian / ω),
                AlmostEquals(series(t0_ + i * Radian / ω), 0, 5376));
  }

  // Projection on a 3rd degree basis introduces significant errors.
  auto const projection3 = Projection<3>(
      ω, series, apodization::Hann<HornerEvaluator>(t_min, t_max), dot);
  for (int i = 0; i <= 100; ++i) {
    EXPECT_THAT(projection3(t0_ + i * Radian / ω),
                RelativeErrorFrom(series(t0_ + i * Radian / ω),
                                  AllOf(Gt(1.0e-10), Lt(2.7e-7))));
  }
}

TEST_F(FrequencyAnalysisTest, PiecewisePoissonSeriesProjection) {
  AngularFrequency const ω = 6.66543 * π * Radian / Second;
  std::mt19937_64 random(42);
  std::uniform_real_distribution<> amplitude_distribution(-10.0, 10.0);
  std::uniform_real_distribution<> perturbation_distribution(-1e-6, 1e-6);

  using PiecewiseSeries4 = PiecewisePoissonSeries<Length, 4, HornerEvaluator>;

  auto const sin = random_polynomial4_(t0_, random, amplitude_distribution);
  auto const cos = random_polynomial4_(t0_, random, amplitude_distribution);
  Series4 const series(
      Series4::Polynomial(Series4::Polynomial::Coefficients{}, t0_),
      {{ω, Series4::Polynomials{sin, cos}}});

  // Build a series that is based on |series| with different perturbations over
  // different intervals.
  PiecewiseSeries4 piecewise_series({t0_, t0_ + 1 * Second}, series);
  for (int i = 1; i < 3; ++i) {
    auto const perturbation_sin =
        random_polynomial4_(t0_, random, perturbation_distribution);
    auto const perturbation_cos =
        random_polynomial4_(t0_, random, perturbation_distribution);
    Series4 const perturbation_series(
        Series4::Polynomial(Series4::Polynomial::Coefficients{}, t0_),
        {{ω, Series4::Polynomials{perturbation_sin, perturbation_cos}}});
    piecewise_series.Append({t0_ + i * Second, t0_ + (i + 1) * Second},
                            series + perturbation_series);
  }

  Instant const t_min = piecewise_series.t_min();
  Instant const t_max = piecewise_series.t_max();
  DotImplementation const dot(t_min, t_max);

  // Projection on a 4th degree basis.  The errors are of the order of the
  // perturbation.
  auto const projection4 =
      Projection<4>(ω,
                    piecewise_series,
                    apodization::Hann<HornerEvaluator>(t_min, t_max),
                    dot);
  for (int i = 0; i <= 100; ++i) {
    EXPECT_THAT(projection4(t0_ + i * Radian / ω),
                RelativeErrorFrom(series(t0_ + i * Radian / ω),
                                  AllOf(Gt(6.2e-9), Lt(3.2e-4))));
  }
}

TEST_F(FrequencyAnalysisTest, PoissonSeriesIncrementalProjectionNoSecular) {
  std::mt19937_64 random(42);
  std::uniform_real_distribution<> frequency_distribution(2000.0, 3000.0);

  std::vector<AngularFrequency> ωs;
  std::optional<Series4> series;
  for (int i = 3; i >= 1; --i) {
    std::uniform_real_distribution<> amplitude_distribution(-(1 << i),
                                                            (1 << i));
    ωs.push_back(frequency_distribution(random) * Radian / Second);
    auto const sin = random_polynomial4_(t0_, random, amplitude_distribution);
    auto const cos = random_polynomial4_(t0_, random, amplitude_distribution);
    Series4 const s(
        Series4::Polynomial(Series4::Polynomial::Coefficients{}, t0_),
        {{ωs.back(), Series4::Polynomials{sin, cos}}});
    if (series.has_value()) {
      series.value() += s;
    } else {
      series = s;
    }
  }

  Instant const t_min = t0_;
  Instant const t_max =
      t0_ + 200 * Radian / *std::max_element(ωs.cbegin(), ωs.cend());
  DotImplementation const dot(t_min, t_max);

  // A perfect calculator for the frequencies of the series.
  int ω_index = 0;
  auto angular_frequency_calculator =
      [&series, t_min, t_max, &ω_index, &ωs](
          auto const& residual) -> std::optional<AngularFrequency> {
    for (int i = 0; i <= 100; ++i) {
      EXPECT_THAT(
          Abs(residual(t_min + i * (t_max - t_min) / 100)),
          ω_index == 0
              ? AllOf(Gt(2.9e-2 * Metre), Lt(5.8 * Metre))
              : ω_index == 1
                    ? AllOf(Gt(6.7e-2 * Metre), Lt(7.9 * Metre))
                    : ω_index == 2
                          ? AllOf(Gt(1.1e-4 * Metre), Lt(9.7e-1 * Metre))
                          : AllOf(Gt(4.2e-10 * Metre), Lt(1.7e-5 * Metre)))
          << ω_index;
    }
    if (ω_index == ωs.size()) {
      return std::nullopt;
    } else {
      return ωs[ω_index++];
    }
  };

  // Projection on a 4th degree basis reconstructs the function with a decent
  // accuracy.
  auto const projection4 =
      IncrementalProjection<4>(series.value(),
                               angular_frequency_calculator,
                               apodization::Hann<HornerEvaluator>(t_min, t_max),
                               dot);
  for (int i = 0; i <= 100; ++i) {
    EXPECT_THAT(
        projection4(t_min + i * (t_max - t_min) / 100),
        RelativeErrorFrom(series.value()(t_min + i * (t_max - t_min) / 100),
                          AllOf(Gt(1.3e-10), Lt(5.4e-4))));
  }
}

TEST_F(FrequencyAnalysisTest, PoissonSeriesIncrementalProjectionSecular) {
  std::mt19937_64 random(42);
  std::uniform_real_distribution<> frequency_distribution(2000.0, 3000.0);
  std::uniform_real_distribution<> secular_distribution(-30.0, 30.0);

  std::vector<AngularFrequency> ωs = {AngularFrequency{}};
  Series4 series(
      random_polynomial4_(t0_, random, secular_distribution), {});
  for (int i = 3; i >= 1; --i) {
    std::uniform_real_distribution<> amplitude_distribution(-(1 << i),
                                                            (1 << i));
    ωs.push_back(frequency_distribution(random) * Radian / Second);
    auto const sin = random_polynomial4_(t0_, random, amplitude_distribution);
    auto const cos = random_polynomial4_(t0_, random, amplitude_distribution);
    series += Series4(
        Series4::Polynomial(Series4::Polynomial::Coefficients{}, t0_),
        {{ωs.back(), Series4::Polynomials{sin, cos}}});
  }

  Instant const t_min = t0_;
  Instant const t_max =
      t0_ + 200 * Radian / *std::max_element(ωs.cbegin(), ωs.cend());
  DotImplementation const dot(t_min, t_max);

  // A perfect calculator for the frequencies of the series.
  int ω_index = 0;
  auto angular_frequency_calculator =
      [&series, t_min, t_max, &ω_index, &ωs](
          auto const& residual) -> std::optional<AngularFrequency> {
    for (int i = 0; i <= 100; ++i) {
      EXPECT_THAT(
          Abs(residual(t_min + i * (t_max - t_min) / 100)),
          ω_index == 0
              ? AllOf(Gt(12.4 * Metre), Lt(19.5 * Metre))
              : ω_index == 1
                    ? AllOf(Gt(8.4e-3 * Metre), Lt(3.7 * Metre))
                    : ω_index == 2
                          ? AllOf(Gt(3.3e-2 * Metre), Lt(3.6 * Metre))
                          : ω_index == 3
                                ? AllOf(Gt(7.5e-3 * Metre), Lt(5.4 * Metre))
                                : AllOf(Gt(2.9e-14 * Metre),
                                        Lt(1.2e-9 * Metre)))
          << ω_index;
    }
    if (ω_index == ωs.size()) {
      return std::nullopt;
    } else {
      return ωs[ω_index++];
    }
  };

  // Projection on a 4th degree basis reconstructs the function with a decent
  // accuracy.
  auto const projection4 =
      IncrementalProjection<4>(series,
                               angular_frequency_calculator,
                               apodization::Hann<HornerEvaluator>(t_min, t_max),
                               dot);
  for (int i = 0; i <= 100; ++i) {
    EXPECT_THAT(
        projection4(t_min + i * (t_max - t_min) / 100),
        RelativeErrorFrom(series(t_min + i * (t_max - t_min) / 100),
                          AllOf(Gt(1.6e-15), Lt(6.6e-11))));
  }
}

}  // namespace frequency_analysis
}  // namespace numerics
}  // namespace principia
