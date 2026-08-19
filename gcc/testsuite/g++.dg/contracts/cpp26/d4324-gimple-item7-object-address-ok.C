// D4324/P2680 item 7's Q1 (implicit is_object_address for a conveyor
// callee's reference parameter), ported to the built-in GIMPLE-pass
// engine (see gcc/cp/contracts-gimple.cc's own cg_check_call_
// reference_safety) -- previously entirely absent from GIMPLE (Tier 3a
// correction, see .claude/plans/lazy-stirring-pearl.md): a reference
// parameter of a DECL_DECLARED_CONVEYOR_P callee now implicitly
// requires is_object_address, triggered by parameter TYPE alone,
// exactly mirroring contracts.cc's own oa_handle_call_precondition_
// obligation. Gated by -fcontract-conveyor-proofs-gimple, run alongside
// the ordinary mandatory AST-level check -- both must independently
// accept this.
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

struct T { int v; };
int use_val_const (const T& x) conveyor { return x.v; }

int q1_ok () conveyor
{
  T y{3};
  return use_val_const (y);
}

int main () { return q1_ok () - 3; }
