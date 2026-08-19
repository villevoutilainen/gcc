// Companion to d4324-gimple-item8-divmod-ok.C: an unprovable divisor,
// for both div and mod. Rejected by contracts.cc's own mandatory,
// unconditional item 8 pass regardless of any GIMPLE flag (confirmed
// by direct testing: the identical errors fire even with
// -fcontract-conveyor-proofs-gimple entirely absent) -- so, exactly
// like item 7's own violation tests, this can only demonstrate
// "compilation still correctly rejects this with both engines active,"
// not GIMPLE's own check in isolation (item 8's AST-side trigger has
// no known gap the way item 7's calling-function trigger does -- see
// this file's own leading comment in contracts-gimple.cc for the
// cg_check_div_mod_ub / cg_provably_nonzero_for_ub_p pair that mirrors
// this regardless).
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

int div_bad (int a, int b) conveyor
{
  return a / b; // { dg-error "not provably nonzero" }
}

int mod_bad (int a, int b) conveyor
{
  return a % b; // { dg-error "not provably nonzero" }
}

int main () { return 0; }
