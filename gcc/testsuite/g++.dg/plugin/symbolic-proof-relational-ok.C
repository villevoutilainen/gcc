// symbolic_proof_plugin.cc: OA_PROVEN_TRUE for a relational
// precondition against another of the callee's own parameters
// ("pre<ctrl>(x < q)", q not a literal) -- g's own self-trusted
// "x < q" (its own parameters) is discharged against f's identically-
// shaped precondition once both parameters are forwarded unchanged, no
// -fcontract-symbolic-proofs needed, since oa_walk_function_calls arms
// the shared relational-fact substrate on its own. See .claude/plans/
// well-we-last-discussed-ethereal-duckling.md.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

int f (int x, int const q) pre<symbolic_ctrl_v> (x < q) { return x; }
int g (int x, int const q) pre<symbolic_ctrl_v> (x < q) { return f (x, q); }

int main () { return g (2, 5) - 2; }
