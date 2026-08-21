// A proven_conveyor port of jonnygrant/compile_assert's
// testsuite/main28_a.cpp, restoring the *full* original intent -- the
// value stays clamped within [0, 100], not just non-negative (see
// d4324-proven-conveyor-number-class-ok.C for the weaker, unclamped
// version, and d4324-proven-symbolic-number-class-positive-ok.C for why
// this same strengthening is not achievable under proven_symbolic).
//
// The two methods are NOT symmetric here:
//
// decrease_by: the full postcondition (>= 0.0 && <= 100.0) is provable
// with NO body change -- this->m_value and percentage are both bounded
// individually on *both* sides (fully two-sided), so
// factor = 1 - percentage/100 is fully bounded in [0, 1], and the
// product of two fully-bounded operands (m_value in [0,100], factor in
// [0,1]) is exactly [0, 100] via ordinary interval multiplication.
//
// increase_by: the full postcondition is mathematically FALSE for a
// purely multiplicative body, for ANY precondition -- if this->m_value
// is already at its allowed maximum (100) and percentage is any
// positive value, 'm_value *= (1 + percentage/100)' exceeds 100. That's
// not a proof gap, it's a genuine counterexample. Restoring the full
// [0, 100] guarantee requires an explicit clamp in the body. The
// multiplication itself only needs to derive a *lower* bound here (>=
// 0.0, from this->m_value's own lower bound times a non-negative
// factor) -- exactly the partial-bound interval-multiplication case
// this session's D4324 fix added -- and the subsequent if-narrowing
// (an unrelated, pre-existing engine capability) caps the upper bound
// to exactly 100.0 across both branches.
//
// value() calls itself only once in main() -- calling a const accessor
// on the same object twice in one expression was found, separately, to
// conservatively invalidate the object's own cached field facts between
// the two calls (unrelated to anything this test is about), so the
// result is captured in a local instead.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct Number
{
  double m_value;

  explicit Number (double value)
    pre<sc::proven_conveyor_v>(value >= 0.0 && value <= 100.0)
    post<sc::proven_conveyor_v>(this->m_value >= 0.0 && this->m_value <= 100.0)
  { m_value = value; }

  void increase_by (double percentage)
    pre<sc::proven_conveyor_v>(std::is_object_address (this))
    pre<sc::proven_conveyor_v>(percentage >= 0.0
				 && this->m_value >= 0.0 && this->m_value <= 100.0)
    post<sc::proven_conveyor_v>(this->m_value >= 0.0 && this->m_value <= 100.0)
  {
    m_value *= (1.0 + percentage / 100.0);
    if (m_value > 100.0)
      m_value = 100.0;
  }

  void decrease_by (double percentage)
    pre<sc::proven_conveyor_v>(std::is_object_address (this))
    pre<sc::proven_conveyor_v>(percentage >= 0.0 && percentage <= 100.0
				 && this->m_value >= 0.0 && this->m_value <= 100.0)
    post<sc::proven_conveyor_v>(this->m_value >= 0.0 && this->m_value <= 100.0)
  { m_value *= (1.0 - percentage / 100.0); }

  double value () const
    pre<sc::proven_conveyor_v>(std::is_object_address (this))
    pre<sc::proven_conveyor_v>(this->m_value >= 0.0 && this->m_value <= 100.0)
    post<sc::proven_conveyor_v>(r: r >= 0.0 && r <= 100.0)
  { return m_value; }
};

int
main ()
{
  Number n (50.0);
  n.increase_by (100.0);
  n.decrease_by (20.0);
  double v = n.value ();
  return v >= 0.0 && v <= 100.0 ? 0 : 1;
}
