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
// A postcondition's own claim is never verified against its function's
// own body under proven_symbolic (self-check is conveyor-only) -- it is
// trusted outright and established for callers, exactly like an
// ordinary precondition is trusted for a function's own body. This is
// why the constructor and both mutators each declare an explicit
// post<>(this->m_value >= 0.0): a plain field write ('m_value = ...;')
// does not itself establish a field-range fact the way a plain scalar
// assignment does (see d4324-conveyor-proof-field-range-ok.C's own
// convention) -- only a declared contract or a real if-condition does.
//
// See d4324-proven-symbolic-number-class-bad.C for the genuine
// violation this same class also demonstrates.
// { dg-do run { target c++26 } }
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
    post<sc::proven_symbolic_v>(this->m_value >= 0.0)
  { m_value *= (1.0 + percentage / 100.0); }

  void decrease_by (double percentage)
    pre<sc::proven_symbolic_v>(percentage >= 0.0 && percentage <= 100.0)
    post<sc::proven_symbolic_v>(this->m_value >= 0.0)
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
