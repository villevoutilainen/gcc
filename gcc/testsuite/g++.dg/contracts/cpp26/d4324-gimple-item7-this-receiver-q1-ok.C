// D4324/P2680 author correction (2026-08-19): a member conveyor call's
// own receiver ('this' of the callee) now carries the same implicit
// is_object_address obligation an explicit reference parameter always
// has (cg_check_call_reference_safety's own new is_this_parameter
// block, mirroring contracts.cc's own identical addition). Also caught
// by contracts.cc's own mandatory, unconditional item 7 pass (gated
// purely on flag_contract_control_objects), so this only demonstrates
// "compilation succeeds with both engines active," matching this
// directory's own item7/item8 testing limitation.
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

struct S {
  int v;
  int bump () conveyor { v = 5; return v; }
};

int via_pointer_precondition (S *p) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  return p->bump ();
}

int main () { S s{1}; return via_pointer_precondition (&s) == 5 ? 0 : 1; }
