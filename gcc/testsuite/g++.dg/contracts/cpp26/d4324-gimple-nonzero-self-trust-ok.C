// The built-in GIMPLE-pass engine's own self-trust case for
// nonzero-ness (-fcontract-conveyor-proofs-gimple): g's own declared
// precondition "n != 0" is trusted for the rest of g's own body
// (cg_seed_self_trust seeds ssa_default_def(g, n) into established_nz),
// so the consumer(n) call inside g's own body is discharged purely
// from that seeded SSA fact. See gcc/cp/contracts-gimple.cc and
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

int consumer (int n) pre<conveyor_ctrl_v>(n != 0) { return 10 / n; }

int g (int n) pre<conveyor_ctrl_v>(n != 0) { return consumer (n); }

int main () { return g (5) - 2; }
