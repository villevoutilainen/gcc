// D4324/P2680: -fcontract-conveyor-proof-provenance, if/else-join case,
// OA_PROVEN_TRUE case -- r's established range at the call to consume
// is the *join* of compute_a's postcondition ([10, 49]) and
// compute_b's postcondition ([20, 79]) across the two branches, not a
// single postcondition read directly. With provenance tracking active,
// the emitted certificate derives that established range from the two
// branches via a genuine case split (a fresh branch-condition Bool
// constant + one implication per arm) instead of asserting the flat
// merged interval [10, 79] as a bare, unexplained premise.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-conveyor-proof-provenance -fdump-contract-proofs=range-if-join-ok-proofs.smt2" }
// { dg-final { scan-file range-if-join-ok-proofs.smt2 "\\(declare-const branch_0 Bool\\)" } }
// { dg-final { scan-file range-if-join-ok-proofs.smt2 "\\(assert \\(=> branch_0 \\(and \\(>= v 10\\) \\(<= v 49\\)\\)\\)\\)" } }
// { dg-final { scan-file range-if-join-ok-proofs.smt2 "\\(assert \\(=> \\(not branch_0\\) \\(and \\(>= v 20\\) \\(<= v 79\\)\\)\\)\\)" } }
// { dg-final { scan-file range-if-join-ok-proofs.smt2 "expect: unsat" } }

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

int compute_a () post<conveyor_ctrl_v>(r: r < 50 && r >= 10)
{
  return 20;
}

int compute_b () post<conveyor_ctrl_v>(r: r < 80 && r >= 20)
{
  return 30;
}

void consume (int x) pre<conveyor_ctrl_v>(x < 1000 && x >= 0)
{
  (void) x;
}

void caller (bool flag)
{
  int r;
  if (flag)
    r = compute_a ();
  else
    r = compute_b ();
  consume (r);
}

int main () { caller (true); caller (false); return 0; }
