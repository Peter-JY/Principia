#pragma once

#include "physics/barycentric_rotating_reference_frame.hpp"

#include <algorithm>
#include <utility>
#include <vector>
#include <set>

#include "geometry/barycentre_calculator.hpp"
#include "geometry/r3x3_matrix.hpp"
#include "quantities/quantities.hpp"
#include "quantities/si.hpp"

namespace principia {
namespace physics {
namespace _barycentric_rotating_reference_frame {
namespace internal {

using namespace principia::geometry::_grassmann;
using namespace principia::geometry::_orthogonal_map;
using namespace principia::geometry::_r3x3_matrix;
using namespace principia::quantities::_elementary_functions;
using namespace principia::quantities::_named_quantities;
using namespace principia::quantities::_quantities;
using namespace principia::quantities::_si;

template<typename InertialFrame, typename ThisFrame>
BarycentricRotatingReferenceFrame<InertialFrame, ThisFrame>::
    BarycentricRotatingReferenceFrame(
        not_null<Ephemeris<InertialFrame> const*> ephemeris,
        not_null<MassiveBody const*> primary,
        not_null<MassiveBody const*> secondary)
    : BarycentricRotatingReferenceFrame(
          ephemeris,
          std::vector{primary},
          std::vector<not_null<MassiveBody const*>>{secondary}) {}

template<typename InertialFrame, typename ThisFrame>
BarycentricRotatingReferenceFrame<InertialFrame, ThisFrame>::
    BarycentricRotatingReferenceFrame(
        not_null<Ephemeris<InertialFrame> const*> ephemeris,
        std::vector<not_null<MassiveBody const*>> primaries,
        std::vector<not_null<MassiveBody const*>> secondaries)
    : ephemeris_(std::move(ephemeris)),
      primaries_(std::move(primaries)),
      secondaries_(std::move(secondaries)),
      primary_trajectory_(ephemeris_->trajectory(primaries_.front())) {
  std::set<not_null<MassiveBody const*>> primary_set(primaries_.begin(),
                                                     primaries_.end());
  std::set<not_null<MassiveBody const*>> secondary_set(secondaries_.begin(),
                                                       secondaries_.end());
  std::set<not_null<MassiveBody const*>> intersection;
  std::set_intersection(primary_set.begin(),
                        primary_set.end(),
                        secondary_set.begin(),
                        secondary_set.end(),
                        std::inserter(intersection, intersection.begin()));
  CHECK_GE(primaries_.size(), 1);
  CHECK_EQ(primary_set.size(), primaries_.size()) << primaries_;
  CHECK_GE(secondaries_.size(), 1);
  CHECK_EQ(secondary_set.size(), secondaries_.size()) << secondaries_;
  CHECK_EQ(intersection.size(), 0) << intersection;
}

template<typename InertialFrame, typename ThisFrame>
std::vector<not_null<MassiveBody const*>> const&
BarycentricRotatingReferenceFrame<InertialFrame, ThisFrame>::primaries() const {
  return primaries_;
}

template<typename InertialFrame, typename ThisFrame>
std::vector<not_null<MassiveBody const*>> const&
BarycentricRotatingReferenceFrame<InertialFrame, ThisFrame>::secondaries()
    const {
  return secondaries_;
}

template<typename InertialFrame, typename ThisFrame>
Instant BarycentricRotatingReferenceFrame<InertialFrame, ThisFrame>::t_min()
    const {
  // We depend on all bodies via the gravitational acceleration.
  return ephemeris_->t_min();
}

template<typename InertialFrame, typename ThisFrame>
Instant BarycentricRotatingReferenceFrame<InertialFrame, ThisFrame>::t_max()
    const {
  // We depend on all bodies via the gravitational acceleration.
  return ephemeris_->t_max();
}

template<typename InertialFrame, typename ThisFrame>
RigidMotion<InertialFrame, ThisFrame>
BarycentricRotatingReferenceFrame<InertialFrame, ThisFrame>::ToThisFrameAtTime(
    Instant const& t) const {
  BarycentreCalculator<DegreesOfFreedom<InertialFrame>, GravitationalParameter>
      primary_degrees_of_freedom;
  BarycentreCalculator<Vector<Acceleration, InertialFrame>,
                       GravitationalParameter>
      primary_acceleration;
  for (not_null const primary : primaries_) {
    primary_degrees_of_freedom.Add(
        ephemeris_->trajectory(primary)->EvaluateDegreesOfFreedom(t),
        primary->gravitational_parameter());
    primary_acceleration.Add(
        ephemeris_->ComputeGravitationalAccelerationOnMassiveBody(primary, t),
        primary->gravitational_parameter());
  }

  BarycentreCalculator<DegreesOfFreedom<InertialFrame>, GravitationalParameter>
      secondary_degrees_of_freedom;
  BarycentreCalculator<Vector<Acceleration, InertialFrame>,
                       GravitationalParameter>
      secondary_acceleration;
  for (not_null const secondary : secondaries_) {
    secondary_degrees_of_freedom.Add(
        ephemeris_->trajectory(secondary)->EvaluateDegreesOfFreedom(t),
        secondary->gravitational_parameter());
    secondary_acceleration.Add(
        ephemeris_->ComputeGravitationalAccelerationOnMassiveBody(secondary, t),
        secondary->gravitational_parameter());
  }

  return ToThisFrame(primary_degrees_of_freedom,
                     secondary_degrees_of_freedom,
                     primary_acceleration.Get(),
                     secondary_acceleration.Get());
}

template<typename InertialFrame, typename ThisFrame>
void BarycentricRotatingReferenceFrame<InertialFrame, ThisFrame>::
WriteToMessage(not_null<serialization::ReferenceFrame*> const message) const {
  auto* const extension = message->MutableExtension(
      serialization::BarycentricRotatingReferenceFrame::extension);
  for (not_null const primary : primaries_) {
    extension->add_primary(
        ephemeris_->serialization_index_for_body(primary));
  }
  for (not_null const secondary : secondaries_) {
    extension->add_secondary(
        ephemeris_->serialization_index_for_body(secondary));
  }
}

template<typename InertialFrame, typename ThisFrame>
not_null<std::unique_ptr<
    BarycentricRotatingReferenceFrame<InertialFrame, ThisFrame>>>
BarycentricRotatingReferenceFrame<InertialFrame, ThisFrame>::ReadFromMessage(
    not_null<Ephemeris<InertialFrame> const*> const ephemeris,
    serialization::BarycentricRotatingReferenceFrame const& message) {
  std::vector<not_null<MassiveBody const*>> primaries;
  primaries.reserve(message.primary().size());
  for (int const primary : message.primary()) {
    primaries.push_back(ephemeris->body_for_serialization_index(primary));
  }
  std::vector<not_null<MassiveBody const*>> secondaries;
  secondaries.reserve(message.secondary().size());
  for (int const secondary : message.secondary()) {
    secondaries.push_back(ephemeris->body_for_serialization_index(secondary));
  }
  return std::make_unique<BarycentricRotatingReferenceFrame>(
      ephemeris, std::move(primaries), std::move(secondaries));
}

template<typename InertialFrame, typename ThisFrame>
Vector<Acceleration, InertialFrame>
BarycentricRotatingReferenceFrame<InertialFrame, ThisFrame>::
GravitationalAcceleration(Instant const& t,
                          Position<InertialFrame> const& q) const {
  return ephemeris_->ComputeGravitationalAccelerationOnMasslessBody(q, t);
}

template<typename InertialFrame, typename ThisFrame>
SpecificEnergy BarycentricRotatingReferenceFrame<InertialFrame, ThisFrame>::
GravitationalPotential(Instant const& t,
                       Position<InertialFrame> const& q) const {
  return ephemeris_->ComputeGravitationalPotential(q, t);
}

template<typename InertialFrame, typename ThisFrame>
AcceleratedRigidMotion<InertialFrame, ThisFrame>
BarycentricRotatingReferenceFrame<InertialFrame, ThisFrame>::MotionOfThisFrame(
    Instant const& t) const {
  BarycentreCalculator<DegreesOfFreedom<InertialFrame>, GravitationalParameter>
      primary_degrees_of_freedom;
  BarycentreCalculator<Vector<Acceleration, InertialFrame>,
                       GravitationalParameter>
      primary_acceleration;
  BarycentreCalculator<Vector<Jerk, InertialFrame>, GravitationalParameter>
      primary_jerk;
  for (not_null const primary : primaries_) {
    primary_degrees_of_freedom.Add(
        ephemeris_->trajectory(primary)->EvaluateDegreesOfFreedom(t),
        primary->gravitational_parameter());
    primary_acceleration.Add(
        ephemeris_->ComputeGravitationalAccelerationOnMassiveBody(primary, t),
        primary->gravitational_parameter());
    primary_jerk.Add(
        ephemeris_->ComputeGravitationalJerkOnMassiveBody(primary, t),
        primary->gravitational_parameter());
  }

  BarycentreCalculator<DegreesOfFreedom<InertialFrame>, GravitationalParameter>
      secondary_degrees_of_freedom;
  BarycentreCalculator<Vector<Acceleration, InertialFrame>,
                       GravitationalParameter>
      secondary_acceleration;
  BarycentreCalculator<Vector<Jerk, InertialFrame>, GravitationalParameter>
      secondary_jerk;
  for (not_null const secondary : secondaries_) {
    secondary_degrees_of_freedom.Add(
        ephemeris_->trajectory(secondary)->EvaluateDegreesOfFreedom(t),
        secondary->gravitational_parameter());
    secondary_acceleration.Add(
        ephemeris_->ComputeGravitationalAccelerationOnMassiveBody(secondary, t),
        secondary->gravitational_parameter());
    secondary_jerk.Add(
        ephemeris_->ComputeGravitationalJerkOnMassiveBody(secondary, t),
        secondary->gravitational_parameter());
  }

  auto const to_this_frame = ToThisFrame(primary_degrees_of_freedom,
                                         secondary_degrees_of_freedom,
                                         primary_acceleration.Get(),
                                         secondary_acceleration.Get());

  Displacement<InertialFrame> const r =
      secondary_degrees_of_freedom.Get().position() -
      primary_degrees_of_freedom.Get().position();
  Velocity<InertialFrame> const ṙ =
      secondary_degrees_of_freedom.Get().velocity() -
      primary_degrees_of_freedom.Get().velocity();
  Vector<Acceleration, InertialFrame> const r̈ =
      secondary_acceleration.Get() - primary_acceleration.Get();
  Vector<Jerk, InertialFrame> const r⁽³⁾ =
      secondary_jerk.Get() - primary_jerk.Get();

  Trihedron<Length, ArealSpeed> orthogonal;
  Trihedron<double, double> orthonormal;
  Trihedron<Length, ArealSpeed, 1> 𝛛orthogonal;
  Trihedron<double, double, 1> 𝛛orthonormal;
  Trihedron<Length, ArealSpeed, 2> 𝛛²orthogonal;
  Trihedron<double, double, 2> 𝛛²orthonormal;

  Base::ComputeTrihedra(r, ṙ, orthogonal, orthonormal);
  Base::ComputeTrihedraDerivatives(r, ṙ, r̈,
                                   orthogonal, orthonormal,
                                   𝛛orthogonal, 𝛛orthonormal);
  Base::ComputeTrihedraDerivatives2(r, ṙ, r̈, r⁽³⁾,
                                    orthogonal, orthonormal,
                                    𝛛orthogonal, 𝛛orthonormal,
                                    𝛛²orthogonal, 𝛛²orthonormal);

  auto const angular_acceleration_of_to_frame =
      Base::ComputeAngularAcceleration(
          orthonormal, 𝛛orthonormal, 𝛛²orthonormal);

  BarycentreCalculator acceleration_of_to_frame_origin = secondary_acceleration;
  acceleration_of_to_frame_origin.Add(primary_acceleration.Get(),
                                      primary_acceleration.weight());
  return AcceleratedRigidMotion<InertialFrame, ThisFrame>(
             to_this_frame,
             angular_acceleration_of_to_frame,
             acceleration_of_to_frame_origin.Get());}

template<typename InertialFrame, typename ThisFrame>
RigidMotion<InertialFrame, ThisFrame>
BarycentricRotatingReferenceFrame<InertialFrame, ThisFrame>::ToThisFrame(
    BarycentreCalculator<DegreesOfFreedom<InertialFrame>,
                         GravitationalParameter> const&
        primary_degrees_of_freedom,
    BarycentreCalculator<DegreesOfFreedom<InertialFrame>,
                         GravitationalParameter> const&
        secondary_degrees_of_freedom,
    Vector<Acceleration, InertialFrame> const& primary_acceleration,
    Vector<Acceleration, InertialFrame> const& secondary_acceleration) const {
  Rotation<InertialFrame, ThisFrame> rotation =
          Rotation<InertialFrame, ThisFrame>::Identity();
  AngularVelocity<InertialFrame> angular_velocity;
  RigidReferenceFrame<InertialFrame, ThisFrame>::ComputeAngularDegreesOfFreedom(
      primary_degrees_of_freedom.Get(),
      secondary_degrees_of_freedom.Get(),
      primary_acceleration,
      secondary_acceleration,
      rotation,
      angular_velocity);

  BarycentreCalculator barycentre_degrees_of_freedom =
      secondary_degrees_of_freedom;
  barycentre_degrees_of_freedom.Add(primary_degrees_of_freedom.Get(),
                                    primary_degrees_of_freedom.weight());
  RigidTransformation<InertialFrame, ThisFrame> const rigid_transformation(
      barycentre_degrees_of_freedom.Get().position(),
      ThisFrame::origin,
      rotation.template Forget<OrthogonalMap>());
  return RigidMotion<InertialFrame, ThisFrame>(
      rigid_transformation,
      angular_velocity,
      barycentre_degrees_of_freedom.Get().velocity());
}

}  // namespace internal
}  // namespace _barycentric_rotating_reference_frame
}  // namespace physics
}  // namespace principia
