// The built-in GIMPLE-pass engine's own literal-vs-literal case where
// the comparison provably does NOT hold at this call site -- a hard
// error, matching the AST-walk's own error_at for the same shape, and
// this engine's own OA_RANGE_DISJOINT sibling case (cg_check_call).
// Previously misreported via warning_at despite both sides being fully
// known literals -- a real severity bug, not a documented scope
// limitation (see .claude/plans/lazy-stirring-pearl.md, item 2.7).
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
inline constexpr conveyor_ctrl ctrl_v{};

int f (int x, int const q) pre<ctrl_v> (x < q) { return x; }

int caller () { return f (5, 2); } // { dg-error "provably violates the precondition" }
                                    // { dg-message "the required comparison" "established fact" { target *-*-* } .-1 }

int main () { return 0; }
