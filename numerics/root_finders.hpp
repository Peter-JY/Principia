﻿
#pragma once

#include <functional>
#include <vector>

#include "base/array.hpp"
#include "quantities/named_quantities.hpp"

namespace principia {
namespace numerics {
namespace internal_root_finders {

using base::BoundedArray;
using quantities::Derivative;

// Approximates a root of |f| between |lower_bound| and |upper_bound| by
// bisection.  The result is less than one ULP from a root of any continuous
// function agreeing with |f| on the values of |Argument|.
// If |f(lower_bound)| and |f(upper_bound)| are both nonzero, they must be of
// opposite signs.
// TODO(phl): Use Brent's algorithm.
template<typename Argument, typename Function>
Argument Bisect(Function f,
                Argument const& lower_bound,
                Argument const& upper_bound);

// Performs Brent’s procedure |zero| from [Bre73], chapter 4, with an absolute
// tolerance t=0.
template<typename Argument, typename Function>
Argument Brent(Function f,
               Argument const& lower_bound,
               Argument const& upper_bound);

// Performs a golden-section search to find a minimum of |f| between
// |lower_bound| and |upper_bound|.
// TODO(phl): Use Brent's algorithm.
template<typename Argument, typename Function, typename Compare>
Argument GoldenSectionSearch(Function f,
                             Argument const& lower_bound,
                             Argument const& upper_bound,
                             Compare compare);

// Performs Brent’s procedure |localmin| from [Bre73], chapter 5.
template<typename Argument, typename Function, typename Compare>
Argument Brent(Function f,
               Argument const& lower_bound,
               Argument const& upper_bound,
               Compare compare);

// Returns the solutions of the quadratic equation:
//   a2 * (x - origin)^2 + a1 * (x - origin) + a0 == 0
// The result may have 0, 1 or 2 values and is sorted.
template<typename Argument, typename Value>
BoundedArray<Argument, 2> SolveQuadraticEquation(
    Argument const& origin,
    Value const& a0,
    Derivative<Value, Argument> const& a1,
    Derivative<Derivative<Value, Argument>, Argument> const& a2);

}  // namespace internal_root_finders

using internal_root_finders::Bisect;
using internal_root_finders::Brent;
using internal_root_finders::GoldenSectionSearch;
using internal_root_finders::SolveQuadraticEquation;

}  // namespace numerics
}  // namespace principia

#include "numerics/root_finders_body.hpp"
