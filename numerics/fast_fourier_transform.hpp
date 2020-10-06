﻿
#pragma once

#include <array>
#include <complex>
#include <map>
#include <type_traits>
#include <vector>

#include "base/bits.hpp"
#include "geometry/complexification.hpp"
#include "geometry/hilbert.hpp"
#include "geometry/interval.hpp"
#include "quantities/named_quantities.hpp"

namespace principia {
namespace numerics {
namespace internal_fast_fourier_transform {

using base::FloorLog2;
using geometry::Complexification;
using geometry::Hilbert;
using geometry::Interval;
using quantities::AngularFrequency;
using quantities::Time;

// This class computes Fourier[{...}, FourierParameters -> {1, -1}] in
// Mathematica notation (the "signal processing" Fourier transform).
template<typename Value, std::size_t size_>
class FastFourierTransform {
 public:
  // The size must be a power of 2.
  static constexpr int size = size_;
  static constexpr int log2_size = FloorLog2(size);
  static_assert(size == 1 << log2_size);

  // In the constructors, the container must have |size| elements.  The samples
  // are assumed to be separated by Δt.

  template<typename Container,
           typename = std::enable_if_t<
               std::is_convertible_v<typename Container::value_type, Value>>>
  FastFourierTransform(Container const& container,
                       Time const& Δt);

  template<typename Iterator,
           typename = std::enable_if_t<std::is_convertible_v<
               typename std::iterator_traits<Iterator>::value_type,
               Value>>>
  FastFourierTransform(Iterator begin, Iterator end,
                       Time const& Δt);

  FastFourierTransform(std::array<Value, size> const& container,
                       Time const& Δt);

  std::map<AngularFrequency, typename Hilbert<Value>::InnerProductType>
  PowerSpectrum() const;

  // Returns the interval that contains the largest peak of power.
  Interval<AngularFrequency> Mode() const;

 private:
  Time const Δt_;
  AngularFrequency const Δω_;

  // The elements of transform_ are spaced in frequency by ω_.
  std::array<Complexification<Value>, size> transform_;

  friend class FastFourierTransformTest;
};

}  // namespace internal_fast_fourier_transform

using internal_fast_fourier_transform::FastFourierTransform;

}  // namespace numerics
}  // namespace principia

#include "numerics/fast_fourier_transform_body.hpp"
