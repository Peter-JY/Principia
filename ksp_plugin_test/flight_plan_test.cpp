﻿
#include "ksp_plugin/flight_plan.hpp"

#include <limits>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "physics/degrees_of_freedom.hpp"
#include "physics/discrete_trajectory.hpp"
#include "physics/massive_body.hpp"
#include "serialization/ksp_plugin.pb.h"
#include "testing_utilities/almost_equals.hpp"
#include "testing_utilities/numerics.hpp"

namespace principia {

using integrators::DormandElMikkawyPrince1986RKN434FM;
using integrators::McLachlanAtela1992Order5Optimal;
using physics::BodyCentredNonRotatingDynamicFrame;
using physics::DegreesOfFreedom;
using physics::DiscreteTrajectory;
using physics::MassiveBody;
using quantities::si::Kilogram;
using quantities::si::Milli;
using quantities::si::Newton;
using testing_utilities::AbsoluteError;
using testing_utilities::AlmostEquals;
using ::testing::AllOf;
using ::testing::Eq;
using ::testing::Gt;
using ::testing::Lt;

namespace ksp_plugin {

class FlightPlanTest : public testing::Test {
 protected:
  using TestNavigationFrame =
      BodyCentredNonRotatingDynamicFrame<Barycentric, Navigation>;

  FlightPlanTest() {
    std::vector<not_null<std::unique_ptr<MassiveBody const>>> bodies;
    bodies.emplace_back(
        make_not_null_unique<MassiveBody>(1 * Pow<3>(Metre) / Pow<2>(Second)));
    std::vector<DegreesOfFreedom<Barycentric>> initial_state{
        {Barycentric::origin, Velocity<Barycentric>()}};
    ephemeris_ = std::make_unique<Ephemeris<Barycentric>>(
        std::move(bodies),
        initial_state,
        /*initial_time=*/t0_ - 2 * π * Second,
        /*fitting_tolerance=*/1 * Milli(Metre),
        Ephemeris<Barycentric>::FixedStepParameters(
            McLachlanAtela1992Order5Optimal<Position<Barycentric>>(),
            /*step=*/1 * Second));
    navigation_frame_ = std::make_unique<TestNavigationFrame>(
        ephemeris_.get(),
        ephemeris_->bodies().back());
    root_.Append(t0_ - 2 * π * Second,
                 {Barycentric::origin + Displacement<Barycentric>(
                                            {1 * Metre, 0 * Metre, 0 * Metre}),
                  Velocity<Barycentric>({0 * Metre / Second,
                                         1 * Metre / Second,
                                         0 * Metre / Second})});
    root_.Append(t0_ + 2 * π * Second,
                 {Barycentric::origin + Displacement<Barycentric>(
                                            {1 * Metre, 0 * Metre, 0 * Metre}),
                  Velocity<Barycentric>({0 * Metre / Second,
                                         1 * Metre / Second,
                                         0 * Metre / Second})});
    flight_plan_ = std::make_unique<FlightPlan>(
        &root_,
        /*initial_time=*/t0_,
        /*final_time=*/t0_ + 1.5 * Second,
        /*initial_mass=*/1 * Kilogram,
        ephemeris_.get(),
        Ephemeris<Barycentric>::AdaptiveStepParameters(
            DormandElMikkawyPrince1986RKN434FM<Position<Barycentric>>(),
            /*max_steps=*/1000,
            /*length_integration_tolerance=*/1 * Milli(Metre),
            /*speed_integration_tolerance=*/1 * Milli(Metre) / Second));
  }

  Burn MakeTangentBurn(Force const& thrust,
                       SpecificImpulse const& specific_impulse,
                       Instant const& initial_time,
                       Speed const& Δv) {
    return {thrust,
            specific_impulse,
            make_not_null_unique<TestNavigationFrame>(*navigation_frame_),
            initial_time,
            Velocity<Frenet<Navigation>>(
                {Δv, 0 * Metre / Second, 0 * Metre / Second})};
  }

  Instant const t0_;
  std::unique_ptr<TestNavigationFrame> navigation_frame_;
  std::unique_ptr<Ephemeris<Barycentric>> ephemeris_;
  DiscreteTrajectory<Barycentric> root_;
  std::unique_ptr<FlightPlan> flight_plan_;
};

using FlightPlanDeathTest = FlightPlanTest;

TEST_F(FlightPlanTest, Singular) {
  // A test mass falling from x₀ = 1 m at vanishing initial speed onto a body
  // with gravitational parameter μ = 1 m³/s².  A singularity occurs for
  // t² = π² (x₀/2)³ / μ.
  auto const& μ = ephemeris_->bodies().back()->gravitational_parameter();
  Length const x0 = 1 * Metre;
  Instant const singularity = t0_ + π * Sqrt(Pow<3>(x0 / 2) / μ);
  flight_plan_.reset();
  root_.ForgetAfter(root_.Begin().time());
  // NOTE(egg): In order for to avoid singular Frenet frames NaNing everything,
  // we offset our test particle by 100 ε.  The resulting system is still
  // extremely stiff, indeed the integrator detects a singularity at the exact
  // same time.  We could avoid doing this if we had absolute direction
  // specification for manœuvres.
  root_.Append(t0_,
               {Barycentric::origin +
                    Displacement<Barycentric>(
                        {x0,
                         100 * std::numeric_limits<double>::epsilon() * Metre,
                         0 * Metre}),
                Velocity<Barycentric>()});
  flight_plan_ = std::make_unique<FlightPlan>(
      &root_,
      /*initial_time=*/t0_,
      /*final_time=*/singularity + 100 * Second,
      /*initial_mass=*/1 * Kilogram,
      ephemeris_.get(),
      Ephemeris<Barycentric>::AdaptiveStepParameters(
          DormandElMikkawyPrince1986RKN434FM<Position<Barycentric>>(),
          /*max_steps=*/1000,
          /*length_integration_tolerance=*/1 * Milli(Metre),
          /*speed_integration_tolerance=*/1 * Milli(Metre) / Second));
  DiscreteTrajectory<Barycentric>::Iterator begin;
  DiscreteTrajectory<Barycentric>::Iterator end;
  flight_plan_->GetSegment(0, &begin, &end);
  DiscreteTrajectory<Barycentric>::Iterator back = end;
  --back;
  EXPECT_THAT(AbsoluteError(singularity, back.time()), Lt(1E-4 * Second));
  // Attempting to put a burn past the singularity fails.
  EXPECT_FALSE(
      flight_plan_->Append(
          MakeTangentBurn(/*thrust=*/1 * Newton,
                          /*specific_impulse=*/1 * Newton * Second / Kilogram,
                          /*initial_time=*/singularity + 1 * Milli(Second),
                          /*Δv=*/1 * Metre / Second)));

  // The singularity occurs during the burn: we're boosting towards the
  // singularity, so we reach the singularity in less than π / 2√2 s, before the
  // end of the burn which lasts 10 (1 - 1/e) s.
  // The derivation of analytic expression for the time at which we reach the
  // singularity is left as an exercise to the reader.
  EXPECT_TRUE(
      flight_plan_->Append(
          MakeTangentBurn(/*thrust=*/1 * Newton,
                          /*specific_impulse=*/1 * Newton * Second / Kilogram,
                          /*initial_time=*/t0_ + 0.5 * Second,
                          /*Δv=*/1 * Metre / Second)));
  flight_plan_->GetSegment(1, &begin, &end);
  back = end;
  --back;
  EXPECT_THAT(back.time(), Lt(singularity));
  EXPECT_NE(begin, back);
  flight_plan_->GetSegment(2, &begin, &end);
  back = end;
  --back;
  EXPECT_EQ(begin, back);

  // The singularity occurs after the burn: we're boosting away from the
  // singularity, so we reach the singularity in more than π / 2√2 s, after the
  // end of the burn which lasts (1 - 1/e)/10 s.
  // The proof of existence of the singularity, as well as the derivation of
  // analytic expression for the time at which we reach the singularity, are
  // left as an exercise to the reader.
  EXPECT_TRUE(
      flight_plan_->ReplaceLast(
          MakeTangentBurn(/*thrust=*/10 * Newton,
                          /*specific_impulse=*/1 * Newton * Second / Kilogram,
                          /*initial_time=*/t0_ + 0.5 * Second,
                          /*Δv=*/-1 * Metre / Second)));
  flight_plan_->GetSegment(1, &begin, &end);
  flight_plan_->GetSegment(1, &begin, &end);
  back = end;
  --back;
  EXPECT_THAT(back.time(), Eq(t0_ + 0.5 * Second + (1 - 1 / e) / 10 * Second));
  EXPECT_NE(begin, back);
  flight_plan_->GetSegment(2, &begin, &end);
  back = end;
  --back;
  EXPECT_THAT(back.time(), AllOf(Gt(singularity), Lt(t0_ + 2 * Second)));
  EXPECT_NE(begin, back);
}

TEST_F(FlightPlanTest, Append) {
  auto const first_burn = [this]() -> Burn {
    return {/*thrust=*/1 * Newton,
            /*specific_impulse=*/1 * Newton * Second / Kilogram,
            make_not_null_unique<TestNavigationFrame>(*navigation_frame_),
            /*initial_time=*/t0_ + 1 * Second,
            Velocity<Frenet<Navigation>>(
                {1 * Metre / Second, 0 * Metre / Second, 0 * Metre / Second})};
  };
  auto const second_burn = [this, first_burn]() -> Burn {
    auto burn = first_burn();
    burn.initial_time += 1 * Second;
    return burn;
  };
  // Burn ends after final time.
  EXPECT_FALSE(flight_plan_->Append(first_burn()));
  EXPECT_EQ(0, flight_plan_->number_of_manœuvres());
  flight_plan_->SetFinalTime(t0_ + 42 * Second);
  EXPECT_TRUE(flight_plan_->Append(first_burn()));
  EXPECT_EQ(1, flight_plan_->number_of_manœuvres());
  EXPECT_FALSE(flight_plan_->Append(first_burn()));
  EXPECT_EQ(1, flight_plan_->number_of_manœuvres());
  EXPECT_TRUE(flight_plan_->Append(second_burn()));
  EXPECT_EQ(2, flight_plan_->number_of_manœuvres());
}

TEST_F(FlightPlanTest, Remove) {
  auto const first_burn = [this]() -> Burn {
    return {/*thrust=*/1 * Newton,
            /*specific_impulse=*/1 * Newton * Second / Kilogram,
            make_not_null_unique<TestNavigationFrame>(*navigation_frame_),
            /*initial_time=*/t0_ + 1 * Second,
            Velocity<Frenet<Navigation>>(
                {1 * Metre / Second, 0 * Metre / Second, 0 * Metre / Second})};
  };
  auto const second_burn = [this, first_burn]() -> Burn {
    auto burn = first_burn();
    burn.initial_time += 1 * Second;
    return burn;
  };
  flight_plan_->SetFinalTime(t0_ + 42 * Second);
  EXPECT_TRUE(flight_plan_->Append(first_burn()));
  EXPECT_TRUE(flight_plan_->Append(second_burn()));
  EXPECT_EQ(2, flight_plan_->number_of_manœuvres());
  flight_plan_->RemoveLast();
  EXPECT_EQ(1, flight_plan_->number_of_manœuvres());
  flight_plan_->RemoveLast();
  EXPECT_EQ(0, flight_plan_->number_of_manœuvres());
  // Check that appending still works.
  EXPECT_TRUE(flight_plan_->Append(first_burn()));
  EXPECT_EQ(1, flight_plan_->number_of_manœuvres());
}

TEST_F(FlightPlanTest, Replace) {
  auto const first_burn = [this]() -> Burn {
    return {/*thrust=*/1 * Newton,
            /*specific_impulse=*/1 * Newton * Second / Kilogram,
            make_not_null_unique<TestNavigationFrame>(*navigation_frame_),
            /*initial_time=*/t0_ + 1 * Second,
            Velocity<Frenet<Navigation>>(
                {1 * Metre / Second, 0 * Metre / Second, 0 * Metre / Second})};
  };
  auto const second_burn = [this, first_burn]() -> Burn {
    auto burn = first_burn();
    burn.Δv *= 10;
    return burn;
  };
  flight_plan_->SetFinalTime(t0_ + 1.7 * Second);
  EXPECT_TRUE(flight_plan_->Append(first_burn()));
  Mass const old_final_mass =
      flight_plan_->GetManœuvre(flight_plan_->number_of_manœuvres() - 1).
          final_mass();
  EXPECT_EQ(1, flight_plan_->number_of_manœuvres());
  EXPECT_FALSE(flight_plan_->ReplaceLast(second_burn()));
  EXPECT_EQ(old_final_mass,
            flight_plan_->GetManœuvre(flight_plan_->number_of_manœuvres() - 1).
                final_mass());
  EXPECT_EQ(1, flight_plan_->number_of_manœuvres());
  flight_plan_->SetFinalTime(t0_ + 42 * Second);
  EXPECT_TRUE(flight_plan_->ReplaceLast(second_burn()));
  EXPECT_GT(old_final_mass,
            flight_plan_->GetManœuvre(flight_plan_->number_of_manœuvres() - 1).
                final_mass());
  EXPECT_EQ(1, flight_plan_->number_of_manœuvres());
}

TEST_F(FlightPlanTest, Segments) {
  auto const first_burn = [this]() -> Burn {
    return {/*thrust=*/1 * Newton,
            /*specific_impulse=*/1 * Newton * Second / Kilogram,
            make_not_null_unique<TestNavigationFrame>(*navigation_frame_),
            /*initial_time=*/t0_ + 1 * Second,
            Velocity<Frenet<Navigation>>(
                {1 * Metre / Second, 0 * Metre / Second, 0 * Metre / Second})};
  };
  auto const second_burn = [this, first_burn]() -> Burn {
    auto burn = first_burn();
    burn.initial_time += 1 * Second;
    return burn;
  };

  flight_plan_->SetFinalTime(t0_ + 42 * Second);
  EXPECT_TRUE(flight_plan_->Append(first_burn()));
  EXPECT_EQ(3, flight_plan_->number_of_segments());
  EXPECT_TRUE(flight_plan_->Append(second_burn()));
  EXPECT_EQ(5, flight_plan_->number_of_segments());

  std::vector<Instant> times;
  DiscreteTrajectory<Barycentric>::Iterator begin;
  DiscreteTrajectory<Barycentric>::Iterator end;

  int last_times_size = times.size();
  Instant last_t = t0_ - 2 * π * Second;
  for (int i = 0; i < flight_plan_->number_of_segments(); ++i) {
    flight_plan_->GetSegment(i, &begin, &end);
    for (auto it = begin; it != end; ++it) {
      Instant const& t = it.time();
      EXPECT_LE(last_t, t);
      EXPECT_LE(t, t0_ + 42 * Second);
      times.push_back(t);
    }
    EXPECT_LT(last_times_size, times.size());
    last_times_size = times.size();
  }
}

TEST_F(FlightPlanTest, Serialization) {
  auto const first_burn = [this]() -> Burn {
    return {/*thrust=*/1 * Newton,
            /*specific_impulse=*/1 * Newton * Second / Kilogram,
            make_not_null_unique<TestNavigationFrame>(*navigation_frame_),
            /*initial_time=*/t0_ + 1 * Second,
            Velocity<Frenet<Navigation>>(
                {1 * Metre / Second, 0 * Metre / Second, 0 * Metre / Second})};
  };
  auto const second_burn = [this, first_burn]() -> Burn {
    auto burn = first_burn();
    burn.initial_time += 1 * Second;
    return burn;
  };

  flight_plan_->SetFinalTime(t0_ + 42 * Second);
  EXPECT_TRUE(flight_plan_->Append(first_burn()));
  EXPECT_TRUE(flight_plan_->Append(second_burn()));

  serialization::FlightPlan message;
  flight_plan_->WriteToMessage(&message);
  EXPECT_TRUE(message.has_initial_mass());
  EXPECT_TRUE(message.has_initial_time());
  EXPECT_TRUE(message.has_final_time());
  EXPECT_TRUE(message.has_adaptive_step_parameters());
  EXPECT_TRUE(message.adaptive_step_parameters().has_integrator());
  EXPECT_TRUE(message.adaptive_step_parameters().has_max_steps());
  EXPECT_TRUE(
      message.adaptive_step_parameters().has_length_integration_tolerance());
  EXPECT_TRUE(
      message.adaptive_step_parameters().has_speed_integration_tolerance());
  EXPECT_EQ(2, message.manoeuvre_size());

  // We need a copy of |root_|.  Might as well do the copy using serialization,
  // since it's how it works in real life.
  serialization::Trajectory serialized_trajectory;
  root_.WriteToMessage(&serialized_trajectory, /*forks=*/{});
  auto const root_read =
      DiscreteTrajectory<Barycentric>::ReadFromMessage(serialized_trajectory,
                                                       /*forks=*/{});

  std::unique_ptr<FlightPlan> flight_plan_read =
      FlightPlan::ReadFromMessage(message, root_read.get(), ephemeris_.get());
  EXPECT_EQ(t0_ - 2 * π * Second, flight_plan_read->initial_time());
  EXPECT_EQ(t0_ + 42 * Second, flight_plan_read->final_time());
  EXPECT_EQ(2, flight_plan_read->number_of_manœuvres());
  EXPECT_EQ(5, flight_plan_read->number_of_segments());
}

}  // namespace ksp_plugin
}  // namespace principia
