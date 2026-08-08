// conveyor_proof_plugin.cc: OA_UNKNOWN for a relational precondition --
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
int g (int x, int q, int const other) pre<conveyor_ctrl_v> (x < q)
{
  return f (x, other); // { dg-warning "cannot verify that .x. satisfies the precondition" }
}

int main () { return g (2, 5, 10) - 2; }
