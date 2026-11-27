// gimple_object_address_plugin.cc: the trivial case -- deref(&x)'s own
// argument is a direct address-of-a-decl, provable regardless of any
// tracked fact (the same ADDR_EXPR short-circuit oa_provable_p itself
// uses). The existing, mandatory AST-level item-7 check (contracts.cc)
// already accepts this silently; this plugin's own, independent GIMPLE/
// SSA-based check must reach the same silent-accept conclusion. See
// ~/gimple-contract-analysis.md.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple -fcontract-analysis-proofs=plugin -fcontract-symbolic-proofs" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

int deref (int* p) pre<symbolic_ctrl_v>(std::is_object_address (p))
{
  return *p;
}

int ok_caller ()
{
  int x = 5;
  return deref (&x);
}

int main () { return ok_caller () - 5; }
