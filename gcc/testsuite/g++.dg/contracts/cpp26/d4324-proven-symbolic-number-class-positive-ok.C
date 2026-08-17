// A proven_symbolic port of jonnygrant/compile_assert's
// testsuite/main28_a.cpp (a Number class wrapping a double, with
// increase_by/decrease_by percentage methods), using this repo's own
// std::contracts pre/post syntax in place of compile_assert's macro-based
// runtime checks. Every method carries as many proven_symbolic
// preconditions/postconditions as this engine can *actually verify*, with
// the single goal of proving m_value stays non-negative -- matching the
// original, which never attempts to bound the value's maximum either.
//
// Every operand feeding a multiplication is bounded *individually*
// (percentage, this->m_value), never as a joint/summed expression -- a
// precondition that only bounds a combination of two variables can never
// be used to prove anything about a *product* of those same two
// variables (this analysis tracks each variable's own range
// independently, with no representation for a relation between two of
// them).
//
// Even with that, increase_by/decrease_by's own postcondition
// (this->m_value >= 0.0) cannot be proven under proven_symbolic, no
// matter how the precondition is written -- a real, deliberate
// architecture boundary, not a bug: oa_get_range/oa_get_float_range's
// general-purpose interval-composition path (used to feed a field's
// value into a multiplication) only ever trusts a *conveyor*-established
// field fact, because a field's own established range can, in general,
// come from an unverified symbolic postcondition claim elsewhere -- so
// composing further arithmetic from it under symbolic could silently
// build on something never actually checked. (A conveyor-flavored port
// of this exact class does NOT have this restriction, since conveyor's
// own field facts carry the stronger "backed by real analysis"
// guarantee -- see d4324-proven-conveyor-number-class-ok.C.) Their
// preconditions are kept below regardless: even though the resulting
// postcondition can't be verified, the precondition itself is still a
// meaningful, trusted axiom for whatever of the body IS reasoned about,
// and documents the intended contract.
//
// value()'s own postcondition has the identical problem, for a more
// basic reason: even a pure 'return m_value;' with no arithmetic at all
// still needs to read the field's own established range through the
// same general-purpose composition path, hitting the same boundary. See
// d4324-proven-symbolic-postcondition-float-field-ok.C for the narrower
// capability this session's field-establishment fix DOES enable: a
// direct, single-hop consult of a field's own range against a literal,
// with no intervening arithmetic and no return-value composition.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct Number
{
  double m_value;

  // Provable: the constructor's own body just stores VALUE directly, no
  // field read is needed to derive this->m_value's own post-state range
  // (only VALUE's own precondition-established range feeds it).
  explicit Number (double value)
    pre<sc::proven_symbolic_v>(value >= 0.0)
    post<sc::proven_symbolic_v>(this->m_value >= 0.0)
  { m_value = value; }

  // Precondition trusted as an axiom; postcondition intentionally
  // omitted -- 'm_value *= (1.0 + percentage / 100.0)' needs
  // this->m_value's own pre-state range as a multiplication operand,
  // which the symbolic flavor's field-composition boundary (see file
  // header) never allows, regardless of what's asserted here.
  void increase_by (double percentage)
    pre<sc::proven_symbolic_v>(percentage >= 0.0 && this->m_value >= 0.0)
  { m_value *= (1.0 + percentage / 100.0); }

  void decrease_by (double percentage)
    pre<sc::proven_symbolic_v>(percentage >= 0.0 && percentage <= 100.0
				 && this->m_value >= 0.0)
  { m_value *= (1.0 - percentage / 100.0); }

  // Precondition trusted as an axiom; postcondition intentionally
  // omitted for the same reason as above, even though the body has no
  // arithmetic at all -- returning m_value still needs to read its own
  // established range through the same boundary.
  double value () const
    pre<sc::proven_symbolic_v>(this->m_value >= 0.0)
  { return m_value; }
};

int
main ()
{
  Number n (50.0);
  n.increase_by (100.0);
  n.decrease_by (20.0);
  return n.value () >= 0.0 ? 0 : 1;
}
