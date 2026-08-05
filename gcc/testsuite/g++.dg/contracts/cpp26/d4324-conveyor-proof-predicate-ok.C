// D4324/P2680: -fcontract-conveyor-proofs, predicate-chaining proof,
// OA_PROVEN_TRUE case -- connects produce's postcondition
// ("check_it (r)") to consume's precondition ("check_it (x)") purely by
// name + argument identity.  check_it is a conveyor function; its
// definition is trusted by construction, so the connection holds
// without ever evaluating it.  Backed by the real, cross-statement-
// tracked fact engine (m_predicate_fact_map), so the value must be
// named by an intermediate variable first for oa_object_identity_decl
// to have a stable key to track it by.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fdump-contract-proofs=predicate-ok-proofs.smt2" }
// { dg-final { scan-file predicate-ok-proofs.smt2 "\\(declare-const check_it Bool\\)" } }
// { dg-final { scan-file predicate-ok-proofs.smt2 "\\(assert check_it\\)\n\\(assert \\(not check_it\\)\\)" } }
// { dg-final { scan-file predicate-ok-proofs.smt2 "expect: unsat" } }

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

int produce () post<conveyor_ctrl_v>(r: check_it (r))
{
  return 1;
}

void consume (int x) pre<conveyor_ctrl_v>(check_it (x))
{
  (void) x;
}

void caller ()
{
  int r = produce ();
  consume (r);
}

int main () { caller (); return 0; }
