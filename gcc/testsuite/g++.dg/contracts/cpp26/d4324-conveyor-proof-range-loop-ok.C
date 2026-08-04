// D4324/P2680: -fcontract-conveyor-proof-provenance, loop-invariant
// case, OA_PROVEN_TRUE case -- d's established range at the call to
// consume_wide is the union of compute_before's postcondition ([1, 4],
// the value entering the loop) and compute_step's postcondition
// ([50, 99], one iteration-independent execution of the loop body's own
// reassignment) -- a sound loop-invariant argument by construction (see
// oa_handle_loop's own comment and .claude/plans/stateless-jumping-
// shore.md). With provenance tracking active, the emitted certificate
// derives that established range as a bare disjunction of the two
// arms' own bounds, instead of asserting the flat merged interval
// [1, 99] as an unexplained premise -- no branch selector is needed at
// all here (unlike the if/else-join case), since there is no condition
// to select between "zero iterations" and "at least one."
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-conveyor-proof-provenance -fdump-contract-proofs=range-loop-ok-proofs.smt2" }
// { dg-final { scan-file range-loop-ok-proofs.smt2 "\\(assert \\(or \\(and \\(>= v 1\\) \\(<= v 4\\)\\) \\(and \\(>= v 50\\) \\(<= v 99\\)\\)\\)\\)" } }
// { dg-final { scan-file range-loop-ok-proofs.smt2 "expect: unsat" } }

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

int compute_before () post<conveyor_ctrl_v>(r: r < 5 && r >= 1)
{
  return 2;
}

int compute_step () post<conveyor_ctrl_v>(r: r < 100 && r >= 50)
{
  return 60;
}

void consume_wide (int x) pre<conveyor_ctrl_v>(x < 1000 && x >= 0)
{
  (void) x;
}

void caller (int n)
{
  int d = compute_before ();
  for (int i = 0; i < n; ++i)
    d = compute_step ();
  consume_wide (d);
}

int main () { caller (3); return 0; }
