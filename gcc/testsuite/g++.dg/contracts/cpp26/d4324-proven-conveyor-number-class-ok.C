// D4324: a proven_conveyor port of jonnygrant/compile_assert's
// testsuite/main28_a.cpp (a Number class wrapping a double, with
// increase_by/decrease_by percentage methods) -- see godbolt.org/z/PPe8nsb5E
// for the original, unmodified shape this is based on. That original
// version does NOT compile clean under proven_conveyor: its precondition
// only bounds the *sum* 'percentage + this->m_value' (not each operand
// individually), so increase_by/decrease_by's own postcondition self-check
// can never derive a range for the multiplicative body update from that
// alone (a genuine limitation of this range-composition-only engine, not
// fixable by any interval-arithmetic correction -- it would need relational
// reasoning between two independently-varying operands). This version
// instead bounds 'this->m_value' and 'percentage' each individually in
// their own right, which the two D4324 corrections below now make provable.
//
// D4324 correction 1: oa_range_multiply/oa_float_range_multiply used to
// require BOTH operands fully two-sided-bounded, declining entirely
// whenever either side was only partially (one-sided) bounded -- even when
// that operand's own SIGN was still fully known, which suffices to derive
// at least a one-sided product bound. increase_by's own factor '(1.0 +
// percentage/100.0)' has no useful upper bound (percentage's own upper
// bound is unconstrained here, deliberately, to exercise exactly this
// case), so this->m_value's own postcondition claim (>= 0.0 only, not a
// tight two-sided range) now derives correctly: nonneg times nonneg is
// nonneg, regardless of whether either side's upper bound is known.
// oa_range_divide/oa_float_range_divide needed the identical correction
// for their own dividend (the divisor -- 100.0 here -- was always a fully-
// bounded, sign-determinate constant already; only the dividend's
// partial-bound requirement was relaxed), since 'percentage / 100.0' is
// itself a division with a partially-bounded dividend.
//
// D4324 correction 2: a partial-bound product/quotient reaching this
// point was still not enough on its own -- the postcondition merge
// snapshot never established a FLOAT range fact for a postcondition's own
// named result identifier at all (only the integer counterpart, via
// oa_get_range/range_set, existed; the float counterpart, via
// oa_get_float_range/float_range_set, was simply missing). This class
// doesn't use a named result identifier itself, but the same merge
// snapshot code path is shared by every postcondition self-check, so this
// fix was required before either correction above could actually reach a
// floating-point postcondition's own verification.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct Number
{
  double m_value;

  explicit Number (double value)
    pre<sc::proven_conveyor_v>(value >= 0.0)
    post<sc::proven_conveyor_v>(this->m_value >= 0.0)
  { m_value = value; }

  void increase_by (double percentage)
    pre<sc::proven_conveyor_v>(percentage >= 0.0 && this->m_value >= 0.0)
    post<sc::proven_conveyor_v>(this->m_value >= 0.0)
  { m_value *= (1.0 + percentage / 100.0); }

  void decrease_by (double percentage)
    pre<sc::proven_conveyor_v>(percentage >= 0.0 && percentage <= 100.0
				 && this->m_value >= 0.0)
    post<sc::proven_conveyor_v>(this->m_value >= 0.0)
  { m_value *= (1.0 - percentage / 100.0); }

  double value () const { return m_value; }
};

int
main ()
{
  Number n (50.0);
  n.increase_by (100.0);
  n.decrease_by (20.0);
  return 0;
}
