// conveyor_proof_plugin.cc: OA_PROVEN_FALSE for a relational
// precondition -- both sides substitute to ordinary compile-time
// literals at this call site (5 and 2), and 5 < 2 is false, so this is
// a genuine, provable violation (plain constant folding, not resolving
// any parameter's own opaque meaning) -- a hard error, the same as any
// other proven-false obligation this plugin already reports. See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do compile }
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

int caller () { return f (5, 2); } // { dg-error "provably violates the precondition" }

int main () { return 0; }
