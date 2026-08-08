// symbolic_proof_plugin.cc: OA_UNKNOWN for a relational precondition --
// g's own self-trust only establishes "x < q" (its own two
// parameters), but the call 'f (x, other)' substitutes f's own second
// parameter with a THIRD, unrelated parameter -- the established
// fact's own RHS (q) doesn't match the substituted RHS (other), so
// this must report "cannot verify". See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
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
int g (int x, int q, int const other) pre<symbolic_ctrl_v> (x < q)
{
  return f (x, other); // { dg-warning "cannot verify that .x. satisfies the precondition" }
}

int main () { return g (2, 5, 10) - 2; }
