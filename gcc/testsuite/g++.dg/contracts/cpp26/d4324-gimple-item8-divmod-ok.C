// Item 8 (div/mod-by-zero) mandatory UB scan, GIMPLE side: two ways to
// prove a divisor nonzero -- an explicit precondition fact, and a
// branch-derived range refinement (the latter needs a context-sensitive
// ranger query at the division statement's own program point, not the
// divisor's whole-function range -- see cg_provably_nonzero_for_ub_p's
// own comment). Both are also caught by contracts.cc's own mandatory,
// unconditional item 8 pass (gated purely on the enclosing function
// being conveyor, no proof flag needed), so this only demonstrates
// "compilation succeeds with both engines active," matching item 7's
// own documented testing limitation -- see this directory's item7 tests.
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

int div_ok (int a, int b) conveyor
  pre<conveyor_ctrl_v>(b != 0)
{
  return a / b;
}

int div_range_ok (int a, int b) conveyor
{
  if (b > 0)
    return a / b;
  return 0;
}

int main () { return div_ok (10, 2) + div_range_ok (10, 2); }
