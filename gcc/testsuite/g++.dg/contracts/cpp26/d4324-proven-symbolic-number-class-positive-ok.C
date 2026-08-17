// A proven_symbolic port of jonnygrant/compile_assert's
// testsuite/main28_a.cpp (a Number class wrapping a double, with
// increase_by/decrease_by percentage methods), using this repo's own
// std::contracts pre/post syntax in place of compile_assert's macro-based
// runtime checks. Every method carries a meaningful proven_symbolic
// precondition and postcondition, with the single goal of proving
// m_value stays non-negative -- matching the original, which never
// attempts to bound the value's maximum either.
//
// Every operand feeding a multiplication is bounded *individually*
// (percentage, this->m_value), never as a joint/summed expression -- a
// precondition that only bounds a combination of two variables can never
// be used to prove anything about a *product* of those same two
// variables (this analysis tracks each variable's own range
// independently, with no representation for a relation between two of
// them).
//
// increase_by/decrease_by/value()'s own postconditions cannot be
// SELF-CHECKED (proven from their own bodies) under proven_symbolic, no
// matter how the precondition is written -- a real, deliberate
// architecture boundary, not a bug: oa_get_range/oa_get_float_range's
// general-purpose interval-composition path (used to feed a field's
// value into a multiplication, or even a plain 'return this->m_value;')
// only ever trusts a *conveyor*-established field fact for that specific
// purpose. (A conveyor-flavored port of this exact class does not have
// this restriction -- see d4324-proven-conveyor-number-class-ok.C.)
//
// This is exactly the situation `never_proven` exists for: the claim is
// true (confirmed separately, e.g. by the conveyor port's own genuinely
// self-checked proof of the same body shape), just not provable by this
// specific engine from these three bodies alone -- the same category as
// a stdlib function whose implementation this engine can't analyze, but
// whose contract should still be trusted by callers. Using it here
// (rather than omitting the postcondition, as an earlier version of this
// file did) is what actually restores the ability to chain calls:
// establishing a postcondition's claimed fact for callers does not
// depend on whether it was self-checked or asserted via never_proven
// (see [[project_symbolic_field_composition_boundary]]) -- but if the
// postcondition were simply absent, callers would have nothing to
// satisfy their own field-range precondition checks with, and this
// session's newly-added symbolic field-range precondition obligation
// enforcement would then correctly refuse to prove decrease_by's and
// value()'s own preconditions, exactly as it did before this rewrite.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

// No built-in "never_proven_symbolic" object ships (never_proven is an
// independent trait, not a fourth flavor of its own) -- same hand-rolled
// pattern as d4324-never-proven-symbolic-ok.C.
struct never_proven_symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  static constexpr bool never_proven (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr never_proven_symbolic_ctrl never_proven_symbolic_ctrl_v{};

struct Number
{
  double m_value;

  // Provable and genuinely self-checked: the constructor's own body just
  // stores VALUE directly, no field read is needed to derive
  // this->m_value's own post-state range (only VALUE's own precondition-
  // established range feeds it).
  explicit Number (double value)
    pre<sc::proven_symbolic_v>(value >= 0.0)
    post<sc::proven_symbolic_v>(this->m_value >= 0.0)
  { m_value = value; }

  // Precondition genuinely enforced at every call site (this session's
  // fix); postcondition asserted via never_proven -- true, but not
  // self-checkable from this body under symbolic (see file header).
  void increase_by (double percentage)
    pre<sc::proven_symbolic_v>(percentage >= 0.0 && this->m_value >= 0.0)
    post<never_proven_symbolic_ctrl_v>(this->m_value >= 0.0)
  { m_value *= (1.0 + percentage / 100.0); }

  void decrease_by (double percentage)
    pre<sc::proven_symbolic_v>(percentage >= 0.0 && percentage <= 100.0
				 && this->m_value >= 0.0)
    post<never_proven_symbolic_ctrl_v>(this->m_value >= 0.0)
  { m_value *= (1.0 - percentage / 100.0); }

  double value () const
    pre<sc::proven_symbolic_v>(this->m_value >= 0.0)
    post<never_proven_symbolic_ctrl_v>(r: r >= 0.0)
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
