// .\Release\x64\benchmarks.exe --benchmark_repetitions=3 --benchmark_filter=Planetarium  // NOLINT(whitespace/line_length)

#include "ksp_plugin/planetarium.hpp"

#include <algorithm>

#include "astronomy/time_scales.hpp"
#include "base/status_utilities.hpp"
#include "benchmark/benchmark.h"
#include "geometry/instant.hpp"
#include "geometry/space_transformations.hpp"
#include "geometry/space.hpp"
#include "physics/body_centred_non_rotating_reference_frame.hpp"
#include "physics/discrete_trajectory.hpp"
#include "physics/solar_system.hpp"
#include "testing_utilities/solar_system_factory.hpp"

namespace principia {
namespace geometry {

using namespace principia::astronomy::_time_scales;
using namespace principia::base::_not_null;
using namespace principia::geometry::_frame;
using namespace principia::geometry::_grassmann;
using namespace principia::geometry::_instant;
using namespace principia::geometry::_interval;
using namespace principia::geometry::_orthogonal_map;
using namespace principia::geometry::_perspective;
using namespace principia::geometry::_space_transformations;
using namespace principia::geometry::_rotation;
using namespace principia::geometry::_rp2_point;
using namespace principia::geometry::_sign;
using namespace principia::geometry::_signature;
using namespace principia::geometry::_space;
using namespace principia::integrators::_methods;
using namespace principia::integrators::_symmetric_linear_multistep_integrator;
using namespace principia::ksp_plugin::_frames;
using namespace principia::ksp_plugin::_planetarium;
using namespace principia::physics::_body_centred_non_rotating_reference_frame;
using namespace principia::physics::_degrees_of_freedom;
using namespace principia::physics::_discrete_trajectory;
using namespace principia::physics::_ephemeris;
using namespace principia::physics::_kepler_orbit;
using namespace principia::physics::_massive_body;
using namespace principia::physics::_massless_body;
using namespace principia::physics::_solar_system;
using namespace principia::quantities::_elementary_functions;
using namespace principia::quantities::_quantities;
using namespace principia::quantities::_si;
using namespace principia::testing_utilities::_solar_system_factory;

namespace {

constexpr Length near = 40'000 * Kilo(Metre);
constexpr Length far = 400'000 * Kilo(Metre);
constexpr Length focal = 1 * Metre;

Perspective<Navigation, Camera> PolarPerspective(
    Length const distance_from_earth) {
  using LeftNavigation =
      Frame<struct LeftNavigationTag, Arbitrary, Handedness::Left>;
  return {
      RigidTransformation<Navigation, Camera>(
          Navigation::origin + Displacement<Navigation>(
                                   {0 * Metre, 0 * Metre, distance_from_earth}),
          Camera::origin,
          Rotation<LeftNavigation, Camera>(
              Vector<double, LeftNavigation>({1, 0, 0}),
              Vector<double, LeftNavigation>({0, -1, 0}),
              Bivector<double, LeftNavigation>({0, 0, -1}))
                  .Forget<OrthogonalMap>() *
              Signature<Navigation, LeftNavigation>(
                  Sign::Positive(),
                  Sign::Positive(),
                  DeduceSignReversingOrientation{})
                  .Forget<OrthogonalMap>())
          .Forget<Similarity>(),
      focal};
}

Perspective<Navigation, Camera> EquatorialPerspective(
    Length const distance_from_earth) {
  using LeftNavigation =
      Frame<struct LeftNavigationTag, Arbitrary, Handedness::Left>;
  return {
      RigidTransformation<Navigation, Camera>(
          Navigation::origin + Displacement<Navigation>(
                                   {0 * Metre, distance_from_earth, 0 * Metre}),
          Camera::origin,
          Rotation<LeftNavigation, Camera>(
              Vector<double, LeftNavigation>({1, 0, 0}),
              Vector<double, LeftNavigation>({0, 0, 1}),
              Bivector<double, LeftNavigation>({0, -1, 0}))
                  .Forget<OrthogonalMap>() *
              Signature<Navigation, LeftNavigation>(
                  Sign::Positive(),
                  Sign::Positive(),
                  DeduceSignReversingOrientation{})
                  .Forget<OrthogonalMap>())
          .Forget<Similarity>(),
      focal};
}

class Satellites {
 public:
  Satellites()
      : solar_system_(make_not_null_unique<SolarSystem<Barycentric>>(
            SOLUTION_DIR / "astronomy" / "sol_gravity_model.proto.txt",
            SOLUTION_DIR / "astronomy" /
                "sol_initial_state_jd_2451545_000000000.proto.txt",
            /*ignore_frame=*/true)),
        ephemeris_(solar_system_->MakeEphemeris(
            /*accuracy_parameters=*/{/*fitting_tolerance=*/1 * Milli(Metre),
                                     /*geopotential_tolerance=*/0x1p-24},
            EphemerisParameters())),
        earth_(solar_system_->massive_body(
            *ephemeris_,
            SolarSystemFactory::name(SolarSystemFactory::Earth))),
        earth_centred_inertial_(
            make_not_null_unique<
                BodyCentredNonRotatingReferenceFrame<Barycentric, Navigation>>(
                ephemeris_.get(),
                earth_)) {
    // Two-line elements for GOES-8:
    // 1 23051U 94022A   00004.06628221 -.00000243  00000-0  00000-0 0  9630
    // 2 23051   0.4232  97.7420 0004776 192.8349 121.5613  1.00264613 28364
    constexpr Instant goes_8_epoch = "JD2451548.56628221"_UT1;
    KeplerianElements<Barycentric> goes_8_elements;
    goes_8_elements.inclination = 0.4232 * Degree;
    goes_8_elements.longitude_of_ascending_node = 97.7420 * Degree;
    goes_8_elements.eccentricity = 0.0004776;
    goes_8_elements.argument_of_periapsis = 192.8349 * Degree;
    goes_8_elements.mean_anomaly = 121.5613 * Degree;
    goes_8_elements.mean_motion = 1.00264613 * (2 * π * Radian / Day);

    CHECK_OK(ephemeris_->Prolong(goes_8_epoch));
    KeplerOrbit<Barycentric> const goes_8_orbit(
        *earth_, MasslessBody{}, goes_8_elements, goes_8_epoch);
    CHECK_OK(goes_8_trajectory_.Append(
        goes_8_epoch,
        ephemeris_->trajectory(earth_)->EvaluateDegreesOfFreedom(goes_8_epoch) +
            goes_8_orbit.StateVectors(goes_8_epoch)));
    auto goes_8_instance = ephemeris_->NewInstance(
        {&goes_8_trajectory_},
        Ephemeris<Barycentric>::NoIntrinsicAccelerations,
        HistoryParameters());
    CHECK_OK(ephemeris_->FlowWithFixedStep(goes_8_epoch + 100 * Day,
                                           *goes_8_instance));
  }

  DiscreteTrajectory<Barycentric> const& goes_8_trajectory() const {
    return goes_8_trajectory_;
  }

  Planetarium MakePlanetarium(
      Perspective<Navigation, Camera> const& perspective) const {
    // No dark area, human visual acuity, wide field of view.
    Planetarium::Parameters parameters(
        /*sphere_radius_multiplier=*/1,
        /*angular_resolution=*/0.4 * ArcMinute,
        /*field_of_view=*/90 * Degree);
    return Planetarium(parameters,
                       perspective,
                       ephemeris_.get(),
                       earth_centred_inertial_.get(),
        [](Position<Navigation> const& plotted_point) {
          constexpr auto inverse_scale_factor = 1 / (6000 * Metre);
          return ScaledSpacePoint::FromCoordinates(
              ((plotted_point - Navigation::origin) *
               inverse_scale_factor).coordinates());
        });
  }

 private:
  Ephemeris<Barycentric>::FixedStepParameters EphemerisParameters() {
    return Ephemeris<Barycentric>::FixedStepParameters(
        SymmetricLinearMultistepIntegrator<
            QuinlanTremaine1990Order12,
            Ephemeris<Barycentric>::NewtonianMotionEquation>(),
        /*step=*/10 * Minute);
  }

  Ephemeris<Barycentric>::FixedStepParameters HistoryParameters() {
    return Ephemeris<Barycentric>::FixedStepParameters(
        SymmetricLinearMultistepIntegrator<
            Quinlan1999Order8A,
            Ephemeris<Barycentric>::NewtonianMotionEquation>(),
        /*step=*/10 * Second);
  }

  not_null<std::unique_ptr<SolarSystem<Barycentric>>> const solar_system_;
  not_null<std::unique_ptr<Ephemeris<Barycentric>>> const ephemeris_;
  not_null<MassiveBody const*> const earth_;
  not_null<std::unique_ptr<NavigationFrame>> const earth_centred_inertial_;
  DiscreteTrajectory<Barycentric> goes_8_trajectory_;
};

}  // namespace

void RunBenchmark(benchmark::State& state,
                  Perspective<Navigation, Camera> const& perspective) {
  Satellites satellites;
  Planetarium planetarium = satellites.MakePlanetarium(perspective);
  std::vector<ScaledSpacePoint> line;
  int iterations = 0;
  // This is the time of a lunar eclipse in January 2000.
  constexpr Instant now = "2000-01-21T04:41:30,5"_TT;
  for (auto _ : state) {
    planetarium.PlotMethod3(
        satellites.goes_8_trajectory(),
        satellites.goes_8_trajectory().begin(),
        satellites.goes_8_trajectory().end(),
        now,
        /*reverse=*/false,
        /*add_point=*/
        [&line](ScaledSpacePoint const& point) { line.push_back(point); },
        /*max_points=*/std::numeric_limits<int>::max());
    ++iterations;
  }
  Interval<double> x;
  Interval<double> y;
  Interval<double> z;
  for (auto const& point : line) {
    x.Include(point.x);
    y.Include(point.y);
    z.Include(point.z);
  }
  state.SetLabel((std::stringstream() << line.size() << " points within " << x
                                      << " × " << y << " × " << z)
                     .str());
}

void BM_PlanetariumPlotMethod3NearPolarPerspective(benchmark::State& state) {
  RunBenchmark(state, PolarPerspective(near));
}

void BM_PlanetariumPlotMethod3FarPolarPerspective(benchmark::State& state) {
  RunBenchmark(state, PolarPerspective(far));
}

void BM_PlanetariumPlotMethod3NearEquatorialPerspective(
    benchmark::State& state) {
  RunBenchmark(state, EquatorialPerspective(near));
}

void BM_PlanetariumPlotMethod3FarEquatorialPerspective(
    benchmark::State& state) {
  RunBenchmark(state, EquatorialPerspective(far));
}

BENCHMARK(BM_PlanetariumPlotMethod3NearPolarPerspective)
    ->Unit(benchmark::kMillisecond);
BENCHMARK(BM_PlanetariumPlotMethod3FarPolarPerspective)
    ->Unit(benchmark::kMillisecond);
BENCHMARK(BM_PlanetariumPlotMethod3NearEquatorialPerspective)
    ->Unit(benchmark::kMillisecond);
BENCHMARK(BM_PlanetariumPlotMethod3FarEquatorialPerspective)
    ->Unit(benchmark::kMillisecond);

}  // namespace geometry
}  // namespace principia
