// conveyor_proof_plugin.cc: OA_PROVEN_TRUE for a relational precondition
// against another of the callee's own parameters ("pre<ctrl>(x < q)",
// q not a literal) -- g's own self-trusted "x < q" (its own parameters)
// is discharged against f's identically-shaped precondition once both
// parameters are forwarded unchanged, via the plugin's own new query
// (oa_env_check_relational_fact) against the real fact engine's shared
// relational-fact substrate. See .claude/plans/well-we-last-discussed-
// ethereal-duckling.md.
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

int f (int x, int const q) pre<conveyor_ctrl_v> (x < q) { return x; }
int g (int x, int const q) pre<conveyor_ctrl_v> (x < q) { return f (x, q); }

int main () { return g (2, 5) - 2; }
