// The built-in GIMPLE-pass engine's own general numeric range check
// (see gcc/cp/contracts-gimple.cc and ~/gimple-contract-analysis.md),
// gated by -fcontract-conveyor-proofs-gimple: a literal constant
// within the required range is provable regardless of any tracked
// fact.
// { dg-do run }
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

void consumer (int x) pre<conveyor_ctrl_v>(x >= 20 && x < 100) { (void) x; }

void ok_caller () { consumer (50); }

int main () { ok_caller (); return 0; }
