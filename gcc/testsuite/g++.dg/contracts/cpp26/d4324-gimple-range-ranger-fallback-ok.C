// The built-in GIMPLE-pass engine's own general-dataflow fallback
// (-fcontract-conveyor-proofs-gimple): 'y' was never established via
// any declared precondition/postcondition fact -- its only source is
// an ordinary literal assignment, 'int y = 55;'. cg_established_range_of
// falls back to GCC's own on-demand ranger (class gimple_ranger) for
// exactly this case. See gcc/cp/contracts-gimple.cc and
// ~/gimple-contract-analysis.md.
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

void k () { int y = 55; consumer (y); }

int main () { k (); return 0; }
