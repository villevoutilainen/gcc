// gimple_object_address_plugin.cc: the self-trust case -- g's own
// declared precondition "is_object_address(p)" is trusted for the rest
// of g's own body (seed_self_trust seeds ssa_default_def(g, p) as
// established), so the deref(p) call inside g's own body is discharged
// purely from that seeded SSA fact, never from the trivial ADDR_EXPR
// shortcut (p here is a plain SSA name, not a freshly-taken address).
// The existing, mandatory AST-level check already accepts this
// silently; this plugin's own GIMPLE/SSA-based self-trust mechanism
// must reach the same conclusion. See ~/gimple-contract-analysis.md.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
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

int g (int *p) pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  return deref (p);
}

int main () { int x = 5; return g (&x) - 5; }
