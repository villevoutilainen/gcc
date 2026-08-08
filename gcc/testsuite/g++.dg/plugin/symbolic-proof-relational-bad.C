// symbolic_proof_plugin.cc: OA_PROVEN_FALSE for a relational
// precondition -- both sides substitute to ordinary compile-time
// literals at this call site (5 and 2), and 5 < 2 is false, so this is
// a genuine, provable violation (plain constant folding, not resolving
// any parameter's own opaque meaning). See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
// { dg-do compile }
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

int caller () { return f (5, 2); } // { dg-error "provably violates the precondition" }

int main () { return 0; }
