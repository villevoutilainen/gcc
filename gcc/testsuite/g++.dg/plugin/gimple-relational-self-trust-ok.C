// gimple_object_address_plugin.cc: the self-trust case for a
// relational precondition -- g's own declared precondition "x < q" is
// trusted for the rest of g's own body (seed_self_trust's new
// relational loop, keyed on ssa_default_def), so forwarding both
// parameters unchanged to f's identically-shaped precondition is
// discharged purely by matching established SSA names -- never
// resolving either parameter to any numeric value. See
// ~/gimple-contract-analysis.md.
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

int f (int x, int const q) pre<conveyor_ctrl_v> (x < q) post<conveyor_ctrl_v> (r: r < q) { return x; }
int g (int x, int const q) pre<conveyor_ctrl_v> (x < q) { return f (x, q); }

int main () { return g (2, 5) - 2; }
