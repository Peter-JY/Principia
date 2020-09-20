﻿#pragma once

#include "numerics/fast_fourier_transform.hpp"

#include <map>
#include <optional>

#include "base/bits.hpp"
#include "quantities/elementary_functions.hpp"
#include "quantities/numbers.hpp"
#include "quantities/quantities.hpp"
#include "quantities/si.hpp"

namespace principia {
namespace numerics {
namespace internal_fast_fourier_transform {

using base::BitReversedIncrement;
using quantities::Angle;
using quantities::Sin;
using quantities::si::Radian;
using quantities::si::Unit;
namespace si = quantities::si;

// Implementation of the Danielson-Lánczos algorithm using templates for
// recursion and template specializations for short FFTs [DL42, Myr07].
template<int array_size_, int chunk_size_ = array_size_>
class DanielsonLánczos {
 public:
  static void Transform(typename std::array<std::complex<double>,
                                            array_size_>::iterator const begin);
};

template<int array_size_>
class DanielsonLánczos<array_size_, 1> {
 public:
  static void Transform(typename std::array<std::complex<double>,
                                            array_size_>::iterator const begin);
};

template<int array_size_>
class DanielsonLánczos<array_size_, 2> {
 public:
  static void Transform(typename std::array<std::complex<double>,
                                            array_size_>::iterator const begin);
};

template<int array_size_>
class DanielsonLánczos<array_size_, 4> {
 public:
  static void Transform(typename std::array<std::complex<double>,
                                            array_size_>::iterator const begin);
};

template<int array_size_, int chunk_size_>
void DanielsonLánczos<array_size_, chunk_size_>::Transform(
    typename std::array<std::complex<double>, array_size_>::iterator const
        begin) {
  constexpr int N = chunk_size_;

  DanielsonLánczos<array_size_, N / 2>::Transform(begin);
  DanielsonLánczos<array_size_, N / 2>::Transform(begin + N / 2);

  Angle const θ = π * Radian / N;
  double const sin_θ = Sin(θ);
  double const cos_2θ_minus_1 = -2 * sin_θ * sin_θ;
  double const sin_2θ = Sin(2 * θ);
  // Computing e⁻²ⁱ⁽ᵏ⁺¹⁾ᶿ as e⁻²ⁱᵏᶿ + e⁻²ⁱᵏᶿ (e⁻²ⁱᶿ - 1) rather than e⁻²ⁱᵏᶿe⁻²ⁱᶿ
  // improves accuracy [Myr07].
  std::complex<double> const e⁻²ⁱᶿ_minus_1(cos_2θ_minus_1, -sin_2θ);
  std::complex<double> e⁻²ⁱᵏᶿ = 1;
  auto it = begin;
  for (int k = 0; k < N / 2; ++it, ++k, e⁻²ⁱᵏᶿ += e⁻²ⁱᵏᶿ * (e⁻²ⁱᶿ_minus_1)) {
    auto const t = *(it + N / 2) * e⁻²ⁱᵏᶿ;
    *(it + N / 2) = *it - t;
    *it += t;
  }
}

template<int array_size_>
void DanielsonLánczos<array_size_, 1>::Transform(
    typename std::array<std::complex<double>, array_size_>::iterator const
        begin) {}

template<int array_size_>
void DanielsonLánczos<array_size_, 2>::Transform(
    typename std::array<std::complex<double>, array_size_>::iterator const
        begin) {
  auto const t = *(begin + 1);
  *(begin + 1) = *begin - t;
  *begin += t;
}

template<int array_size_>
void DanielsonLánczos<array_size_, 4>::Transform(
    typename std::array<std::complex<double>, array_size_>::iterator const
        begin) {
  {
    auto const t = *(begin + 1);
    *(begin + 1) = *begin - t;
    *begin += t;
  }
  {
    auto const t = *(begin + 3);
    *(begin + 3) = {(begin + 2)->imag() - t.imag(),
                    t.real() - (begin + 2)->real()};
    *(begin + 2) += t;
  }
  {
    auto const t = *(begin + 2);
    *(begin + 2) = *begin - t;
    *begin += t;
  }
  {
    auto const t = *(begin + 3);
    *(begin + 3) = *(begin + 1) - t;
    *(begin + 1) += t;
  }
}

template<typename Value, std::size_t size_>
template<typename Container, typename>
FastFourierTransform<Value, size_>::FastFourierTransform(
    Container const& container,
    Time const& Δt)
    : FastFourierTransform(container.cbegin(), container.cend(), Δt) {}

template<typename Value, std::size_t size_>
template<typename Iterator, typename>
FastFourierTransform<Value, size_>::FastFourierTransform(
    Iterator const begin,
    Iterator const end,
    Time const& Δt)
    : Δt_(Δt),
      ω_(2 * π * Radian / (size * Δt_)) {
  DCHECK_EQ(size, std::distance(begin, end));

  // Type decay, reindexing, and promotion to complex.
  int bit_reversed_index = 0;
  for (auto it = begin;
       it != end;
       ++it,
       bit_reversed_index = BitReversedIncrement(bit_reversed_index,
                                                 log2_size)) {
    transform_[bit_reversed_index] = *it / si::Unit<Value>;
  }

  DanielsonLánczos<size>::Transform(transform_.begin());
}

template<typename Value, std::size_t size_>
FastFourierTransform<Value, size_>::FastFourierTransform(
    std::array<Value, size> const& container,
    Time const& Δt)
    : FastFourierTransform(container.cbegin(), container.cend(), Δt) {}

template<typename Value, std::size_t size_>
std::map<AngularFrequency, typename Hilbert<Value>::InnerProductType>
FastFourierTransform<Value, size_>::PowerSpectrum() const {
  std::map<AngularFrequency, typename Hilbert<Value>::InnerProductType>
      spectrum;
  int k = 0;
  for (auto const& coefficient : transform_) {
    spectrum.emplace_hint(
        spectrum.end(),
        k * ω_,
        std::norm(coefficient) *
            si::Unit<typename Hilbert<Value>::InnerProductType>);
    ++k;
  }
  return spectrum;
}

template<typename Value, std::size_t size_>
Interval<AngularFrequency> FastFourierTransform<Value, size_>::Mode() const {
  auto const spectrum = PowerSpectrum();
  typename std::map<AngularFrequency,
                    typename Hilbert<Value>::InnerProductType>::const_iterator
      max = spectrum.end();

  // Only look at the first size / 2 + 1 elements because the spectrum is
  // symmetrical.
  auto it = spectrum.begin();
  for (int i = 0; i < size / 2 + 1; ++i, ++it) {
    if (max == spectrum.end() || it->second > max->second) {
      max = it;
    }
  }
  Interval<AngularFrequency> result;
  if (max == spectrum.begin()) {
    result.Include(max->first);
  } else {
    result.Include(std::prev(max)->first);
  }
  result.Include(std::next(max)->first);
  return result;
}

}  // namespace internal_fast_fourier_transform
}  // namespace numerics
}  // namespace principia
