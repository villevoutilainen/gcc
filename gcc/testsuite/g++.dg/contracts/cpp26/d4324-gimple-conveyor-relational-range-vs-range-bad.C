// D4324 bounds-proving demo, GIMPLE mirror of the param-vs-param range-
// vs-range fallback (cg_check_call, mirroring contracts.cc's own
// oa_env_check_relational_fact_1 identical fallback): each side's own
// independently-established scalar range (from an earlier call's own
// postcondition) settles a "param vs param" precondition even with no
// if-condition or self-trust ever explicitly linking the two -- see
// .claude/plans/lazy-stirring-pearl.md.
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

int big_value () post<conveyor_ctrl_v>(r: r > 100) { return 200; }
int small_value () post<conveyor_ctrl_v>(r: r < 5) { return 1; }

void check (int x, int q) pre<conveyor_ctrl_v>(x < q) { (void) x; (void) q; }

void caller ()
{
  int r = big_value ();
  int q = small_value ();
  check (r, q); // { dg-error "provably violates the precondition" }
                // { dg-message "is established \[^\n\]*, but the precondition requires" "established fact" { target *-*-* } .-1 }
}

int main () { return 0; }
