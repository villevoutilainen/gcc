// The built-in GIMPLE-pass engine's own relational precondition check
// (see gcc/cp/contracts-gimple.cc and .claude/plans/well-we-last-
// discussed-ethereal-duckling.md), gated by
// -fcontract-conveyor-proofs-gimple: the trivial case -- both sides
// substitute to ordinary compile-time literals at this call site,
// provable by plain constant folding (oa_relational_literal_holds),
// regardless of any established fact.
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
inline constexpr conveyor_ctrl ctrl_v{};

int f (int x, int const q) pre<ctrl_v> (x < q) { return x; }

int main () { return f (2, 5) - 2; }
