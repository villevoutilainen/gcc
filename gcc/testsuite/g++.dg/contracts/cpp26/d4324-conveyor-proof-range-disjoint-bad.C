// D4324/P2680: -fcontract-conveyor-proofs, combined-range subsumption,
// OA_PROVEN_FALSE case -- compute_range's postcondition establishes the
// combined interval [40, 99], and consume_disjoint's precondition
// requires the combined interval [0, 9].  The two intervals are
// disjoint -- no value satisfying the postcondition can ever satisfy
// the precondition -- so this is a genuine, confirmed violation,
// proven from the two combined ranges as a whole rather than from
// either single conjunct in isolation.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fdump-contract-proofs=range-disjoint-bad-proofs.smt2" }
// { dg-final { scan-file range-disjoint-bad-proofs.smt2 "\\(assert \\(and \\(>= v 0\\) \\(<= v 9\\)\\)\\)" } }
// { dg-final { scan-file range-disjoint-bad-proofs.smt2 "expect: unsat" } }

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

int compute_range () post<conveyor_ctrl_v>(r: r < 100 && r >= 40)
{
  return 50;
}

void consume_disjoint (int x) pre<conveyor_ctrl_v>(x < 10 && x >= 0)
{
  (void) x;
}

void caller ()
{
  consume_disjoint (compute_range ()); // { dg-error "provably violates the precondition" }
}

int main () { caller (); return 0; }
