// Unlike d4324-gimple-item8-dereference-bad.C, this one is caught by
// GIMPLE ALONE: contracts.cc's own oa_walk_stmt only ever scans a
// MODIFY_EXPR/INIT_EXPR's own RHS for item 8 purposes (oa_scan_item8_
// in_expr (&TREE_OPERAND (t, 1), env)) -- its own LHS is never scanned,
// so a store *through* an unprovable pointer ('*p = 5;') is a genuine,
// pre-existing AST-side scope gap, confirmed by direct testing (this
// exact source compiles cleanly with no GIMPLE flag at all).
// cg_check_dereference_ub's own cg_check_one_dereference_candidate is
// called on a GIMPLE_ASSIGN's LHS as well as its RHS1, so GIMPLE
// actually catches this AST misses -- a genuine, isolable improvement,
// not merely "runs alongside AST's mandatory check" the way every other
// item 8 violation test in this directory is. Kept as strictly more
// thorough than AST rather than narrowed to match, since it is sound
// (a store through a pointer not provably denoting a valid object is
// real UB) and the full contracts/plugin suites show no regression from
// keeping it.
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
