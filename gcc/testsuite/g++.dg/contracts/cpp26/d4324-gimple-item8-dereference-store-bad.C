// A store *through* an unprovable pointer ('*p = 5;'). Originally
// (2026-08-19) this was caught by GIMPLE alone: contracts.cc's own
// oa_walk_stmt only ever scanned a MODIFY_EXPR/INIT_EXPR's own RHS for
// item 8 purposes -- its own LHS was never scanned, a genuine AST-side
// scope gap found while porting cg_check_dereference_ub (GIMPLE's own
// analogue, which checks a GIMPLE_ASSIGN's LHS as well as its RHS1) to
// GIMPLE. Fixed directly in contracts.cc (see oa_walk_stmt's own
// MODIFY_EXPR/INIT_EXPR case, now scanning TREE_OPERAND (t, 0) too, same
// day) rather than leaving GIMPLE stricter than AST -- so this now
// matches every other item 8 violation test in this directory: both
// engines independently reject it, "runs alongside AST's mandatory
// check," not GIMPLE-only.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

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

void deref_store_bad (int *p) conveyor
{
  *p = 5; // { dg-error "not provably valid" }
}

int main () { int x; deref_store_bad (&x); return 0; }
