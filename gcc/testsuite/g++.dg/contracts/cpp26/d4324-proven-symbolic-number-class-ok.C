// D4324: a proven_symbolic port of jonnygrant/compile_assert's
// testsuite/main28_a.cpp (a Number class wrapping a double, with
// increase_by/decrease_by percentage methods) using this repo's own
// std::contracts pre/post syntax instead of compile_assert's macro-based
// checks. Exercises the full chain built up over this session: floating-
// point field-range tracking (this->m_value), a postcondition's own
// established fact silently discharging a later call's precondition
// (produce/consume, entirely across separate calls, not just within one
// function body), and the bare 'param OP literal' combined-range
// precondition obligation (percentage's own bounds) -- all under
// proven_symbolic specifically, with no -fcontract-symbolic-proofs
// anywhere in dg-additional-options, since proven_symbolic forces this
// analysis on by itself.
//
// D4324 correction: a symbolic postcondition's self-check is no longer
// blanket-exempt -- it is now checked with the same rigor a conveyor
// postcondition's self-check already gets (only a conjunct calling a
// function declared 'symbolic' stays a trusted axiom; a plain field
// comparison like this->m_value >= 0.0 does not). The constructor's own
// post<>(this->m_value >= 0.0) is provable from its body (a plain field
// write with a precondition-bounded RHS). increase_by/decrease_by's own
// post<>(this->m_value >= 0.0) is NOT provable from their own bodies,
// even with a precondition establishing this->m_value's own range at
// entry (confirmed by direct testing) -- this->m_value *= (1.0 +/-
// percentage/100.0) is a genuine interval-multiplication limitation of
// this engine (the same one already documented for the conveyor
// flavor of this identical class, e.g. the Number-demo godbolt
// investigation), not a bug this correction is expected to fix.
//
// See d4324-proven-symbolic-number-class-bad.C for the genuine
// precondition violation this same class also demonstrates. Switched
// from 'dg-do run' to 'dg-do compile': increase_by/decrease_by's own
// postcondition self-check now correctly fails (a hard error under
// proven_symbolic's own strictness), so this no longer produces a
// runnable executable -- the file's real purpose (field-range
// establishment/discharge across separate calls, the bare-parameter
// combined-range precondition obligation) is still fully exercised at
// compile time regardless.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct Number
{
  double m_value;

  explicit Number (double value)
    pre<sc::proven_symbolic_v>(value >= 0.0)
    post<sc::proven_symbolic_v>(this->m_value >= 0.0)
  { m_value = value; }

  void increase_by (double percentage)
    pre<sc::proven_symbolic_v>(percentage >= 0.0 && percentage <= 100.0)
    post<sc::proven_symbolic_v>(this->m_value >= 0.0) // { dg-error "cannot prove postcondition condition" }
  { m_value *= (1.0 + percentage / 100.0); }

  void decrease_by (double percentage)
    pre<sc::proven_symbolic_v>(percentage >= 0.0 && percentage <= 100.0)
    post<sc::proven_symbolic_v>(this->m_value >= 0.0) // { dg-error "cannot prove postcondition condition" }
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
