#pragma once

#include "numerics/polynomial.hpp"

namespace principia {
namespace numerics {
namespace _legendre {
namespace internal {

template<int degree_, template<typename, typename, int> class Evaluator>
constexpr PolynomialInMonomialBasis<double, double, degree_, Evaluator>
LegendrePolynomial();

}  // namespace internal

using internal::LegendrePolynomial;

}  // namespace _legendre
}  // namespace numerics
}  // namespace principia

#include "numerics/legendre_body.hpp"
