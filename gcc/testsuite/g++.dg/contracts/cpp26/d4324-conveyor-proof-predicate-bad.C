// D4324/P2680: -fcontract-conveyor-proofs, predicate-chaining proof,
// OA_PROVEN_FALSE case -- produce_bad's postcondition guarantees
// "!check_it (r)" -- the exact opposite polarity of what consume's
// precondition requires for that same value.  A genuine, provable
// contradiction, still without ever evaluating check_it itself.  Via
// the real fact-tracking engine, same intermediate-variable requirement
// as d4324-conveyor-proof-predicate-ok.C.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fdump-contract-proofs=predicate-bad-proofs.smt2" }
// { dg-final { scan-file predicate-bad-proofs.smt2 "\\(assert \\(not check_it\\)\\)\n\\(assert check_it\\)" } }
// { dg-final { scan-file predicate-bad-proofs.smt2 "expect: unsat" } }

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

// conveyor must be repeated identically on every redeclaration.
bool check_it (int v) conveyor { return v > 0; }

int produce_bad () post<conveyor_ctrl_v>(r: !check_it (r))
{
  return -1;
}

void consume (int x) pre<conveyor_ctrl_v>(check_it (x))
{
  (void) x;
}

void caller ()
{
  int r = produce_bad ();
  consume (r); // { dg-error "provably violates the precondition" }
}

int main () { caller (); return 0; }
