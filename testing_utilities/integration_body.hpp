#pragma once

#include "testing_utilities/integration.hpp"

#include <tuple>
#include <vector>

#include "astronomy/epoch.hpp"
#include "quantities/elementary_functions.hpp"
#include "quantities/quantities.hpp"
#include "quantities/named_quantities.hpp"

namespace principia {
namespace testing_utilities {
namespace _integration {
namespace internal {

using astronomy::J2000;
using namespace principia::geometry::_grassmann;
using namespace principia::geometry::_named_quantities;
using namespace principia::quantities::_elementary_functions;
using namespace principia::quantities::_named_quantities;
using namespace principia::quantities::_quantities;
using namespace principia::quantities::_si;

inline absl::Status ComputeHarmonicOscillatorAcceleration1D(
    Instant const& t,
    std::vector<Length> const& q,
    std::vector<Acceleration>& result,
    int* const evaluations) {
  result[0] = -q[0] * (si::Unit<Stiffness> / si::Unit<Mass>);
  if (evaluations != nullptr) {
    ++*evaluations;
  }
  return absl::OkStatus();
}

template<typename Frame>
absl::Status ComputeHarmonicOscillatorAcceleration3D(
    Instant const& t,
    std::vector<Position<Frame>> const& q,
    std::vector<Vector<Acceleration, Frame>>& result,
    int* const evaluations) {
  result[0] = (Frame::origin - q[0]) * (si::Unit<Stiffness> / si::Unit<Mass>);
  if (evaluations != nullptr) {
    ++*evaluations;
  }
  return absl::OkStatus();
}

inline absl::Status ComputeHarmonicOscillatorDerivatives1D(
    Instant const& t,
    std::tuple<Length, Speed> const& state,
    std::tuple<Speed, Acceleration>& result,
    int* const evaluations) {
  auto const& [q, v] = state;
  auto& [qʹ, vʹ] = result;
  qʹ = v;
  vʹ = -q * (si::Unit<Stiffness> / si::Unit<Mass>);
  if (evaluations != nullptr) {
    ++*evaluations;
  }
  return absl::OkStatus();
}

inline absl::Status ComputeKeplerAcceleration(
    Instant const& t,
    std::vector<Length> const& q,
    std::vector<Acceleration>& result,
    int* evaluations) {
  auto const r² = q[0] * q[0] + q[1] * q[1];
  auto const minus_μ_over_r³ =
      -si::Unit<GravitationalParameter> * Sqrt(r²) / (r² * r²);
  result[0] = q[0] * minus_μ_over_r³;
  result[1] = q[1] * minus_μ_over_r³;
  if (evaluations != nullptr) {
    ++*evaluations;
  }
  return absl::OkStatus();
}


template<int degree>
absl::Status ComputeЧебышёвPolynomialSecondDerivative(
    Instant const& t,
    std::vector<double> const& ч,
    std::vector<Variation<double>> const& чʹ,
    std::vector<Variation<Variation<double>>>& чʺ,
    int* evaluations) {
  constexpr int n² = degree * degree;
  constexpr auto s² = Pow<2>(Second);
  Time const x = (t - J2000);
  auto const x² = x * x;
  чʺ[0] = (x * чʹ[0] - n² * ч[0]) / (1 * s² - x²);
  if (evaluations != nullptr) {
    ++*evaluations;
  }
  return absl::OkStatus();
}

template<int degree>
absl::Status ComputeLegendrePolynomialSecondDerivative(
    Instant const& t,
    std::vector<double> const& p,
    std::vector<Variation<double>> const& pʹ,
    std::vector<Variation<Variation<double>>>& pʺ,
    int* evaluations) {
  constexpr int n = degree;
  constexpr auto s² = Pow<2>(Second);
  Time const x = (t - J2000);
  auto const x² = x * x;
  pʺ[0] = (2 * x * pʹ[0] - n * (n + 1) * p[0]) / (1 * s² - x²);
  if (evaluations != nullptr) {
    ++*evaluations;
  }
  return absl::OkStatus();
}

}  // namespace internal
}  // namespace _integration
}  // namespace testing_utilities
}  // namespace principia
