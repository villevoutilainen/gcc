// The built-in GIMPLE-pass engine's own is_object_address check (see
// gcc/cp/contracts-gimple.cc and ~/gimple-contract-analysis.md), gated
// by -fcontract-conveyor-proofs-gimple, run alongside the ordinary
// mandatory AST-level check -- both must independently accept this.
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

int deref (int* p) pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  return *p;
}

int ok_caller ()
{
  int x = 5;
  return deref (&x);
}

int main () { return ok_caller () - 5; }
