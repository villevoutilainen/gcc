// D4324 (see .claude/plans/lazy-stirring-pearl.md, Tier 3b): the
// shift-shaped call-relational conjunct ('idx - v.size () > 10', via
// oa_match_shifted_comparison_against_call) was previously recognized
// ONLY at branch-derived establishment (oa_refine_single_comparison,
// from an ordinary 'if'). It was never established from a function's
// own declared precondition (self-trust, for its own body), and never
// consulted at all as one of a *callee's* own obligations -- so
// forwarding an argument between two functions that both declare the
// exact same shift-shaped precondition still silently required no
// proof whatsoever. Both gaps are closed here: CONSUMER's own shift-
// shaped precondition is now a real, checked obligation, and
// PRODUCER's own declared precondition of the same shape is now
// trusted for its own body, so this call is silently discharged.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct conveyor_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

struct S {
  int size () const conveyor
    post<conveyor_ctrl_v>(r: r >= 0 && r <= 5)
  { return 5; }
};

// IDX's own range (item 8's mandatory overflow scan needs both operands
// of 'idx - v.size ()' fully bounded to prove the subtraction itself
// free of overflow -- unrelated to what this test is actually about).
int consumer (const S& v, int idx) conveyor
  pre<conveyor_ctrl_v>(idx >= 0 && idx <= 1000 && idx - v.size () > 10)
{
  return idx;
}

int producer (const S& v, int idx) conveyor
  pre<conveyor_ctrl_v>(idx >= 0 && idx <= 1000 && idx - v.size () > 10)
{
  return consumer (v, idx);
}

// Established via an ordinary 'if' (oa_refine_single_comparison's own
// pre-existing branch-derived establishment for this shape) -- ENTRY
// itself isn't a conveyor function, so it has no declared precondition
// of its own to trust; this if-condition is the mechanism that lets it
// supply the fact PRODUCER's own precondition then checks.
int entry (const S& v, int idx) conveyor
{
  if (idx >= 0 && idx <= 1000 && idx - v.size () > 10)
    {
      producer (v, idx);
      return 0;
    }
  return -1;
}

int main ()
{
  S v;
  return entry (v, 20);
}
