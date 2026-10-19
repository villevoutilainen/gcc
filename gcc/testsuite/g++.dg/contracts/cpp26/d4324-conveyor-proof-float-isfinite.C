// D4324: floating-point proofs are IEEE-faithful: a value only bounded
// on one side may be infinite, and arithmetic on it may make a NaN, for
// which every comparison is false.  A precondition stating
// isfinite (x) rules both out.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct Number
{
  double m_value;
  void decrease_by (double percentage)
    pre<sc::proven_conveyor_v>(std::is_object_address (this))
    pre<sc::proven_conveyor_v>(percentage >= 0.0 && percentage <= 100.0
			       && __builtin_isfinite (this->m_value)
			       && this->m_value >= 0.0)
    post<sc::proven_conveyor_v>(this->m_value >= 0.0)
  { m_value *= (1.0 - percentage / 100.0); }

  /* Without the isfinite: m_value may be +Inf, and inf * 0 a NaN.  */
  void decrease_by_unbounded (double percentage)
    pre<sc::proven_conveyor_v>(std::is_object_address (this))
    pre<sc::proven_conveyor_v>(percentage >= 0.0 && percentage <= 100.0
			       && this->m_value >= 0.0)
    post<sc::proven_conveyor_v>(this->m_value >= 0.0) // { dg-error "cannot prove postcondition" }
  { m_value *= (1.0 - percentage / 100.0); }
};

double times_zero (double x)
  pre<sc::proven_conveyor_v>(__builtin_isfinite (x) && x >= 0.0)
  post<sc::proven_conveyor_v>(r: r >= 0.0)
{ return x * 0.0; }

double times_zero_unbounded (double x)
  pre<sc::proven_conveyor_v>(x >= 0.0)
  post<sc::proven_conveyor_v>(r: r >= 0.0) // { dg-error "cannot prove postcondition" }
{ return x * 0.0; }

