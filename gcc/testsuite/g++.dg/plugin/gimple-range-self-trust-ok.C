// gimple_object_address_plugin.cc: the self-trust case -- g's own
// declared precondition "x >= 20 && x < 100" is trusted for the rest of
// g's own body (seed_self_trust's own range-grouping logic combines
// both conjuncts into one interval, seeded onto ssa_default_def(g, x)
// in ESTABLISHED_RANGE), so the consumer(x) call inside g's own body is
// discharged purely from that seeded interval, never from the trivial
// literal-constant shortcut. See ~/gimple-contract-analysis.md.
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

void consumer (int x) pre<conveyor_ctrl_v>(x >= 20 && x < 100) { (void) x; }

void g (int x) pre<conveyor_ctrl_v>(x >= 20 && x < 100) { consumer (x); }

int main () { g (50); return 0; }
