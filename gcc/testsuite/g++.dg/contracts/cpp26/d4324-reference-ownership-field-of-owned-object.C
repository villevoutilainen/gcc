// D4324/P2680 Q2 (ownership/cone-of-evaluation), AST-side counterpart
// of d4324-gimple-item7-ownership-field-of-owned-object-ok.C: a FIELD
// of an owned object is itself owned, across all four shapes an owned
// object can take -- 'this->field' (a member conveyor function),
// 'ptr->field' (an ordinary pointer parameter), 'ref.field' (a
// received reference parameter), and 'local.field' (a local object
// this function created itself). The AST engine (oa_reference_owned_p)
// already composed all four correctly even before the GIMPLE-side fix
// (confirmed via direct testing, found during the comprehensive
// coverage sweep, 2026-10-12) -- this file exists as a regression guard
// for that, and to keep both engines' own coverage of this exact
// scenario in parity.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

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

struct Inner { int v; };
int use_val_mut (Inner& x) conveyor { x.v = 5; return x.v; }

struct Outer {
  Inner inner;
  int via_this () conveyor { return use_val_mut (inner); }
};

int via_pointer_parm (Outer *op) conveyor
  pre<conveyor_ctrl_v> (std::is_object_address (op))
{
  return use_val_mut (op->inner);
}

int via_reference_parm (Outer &obj) conveyor
{
  return use_val_mut (obj.inner);
}

int via_local () conveyor
{
  Outer local{{1}};
  return use_val_mut (local.inner);
}

int main ()
{
  Outer o1{{1}};
  if (o1.via_this () != 5)
    return 1;
  Outer o2{{1}};
  if (via_pointer_parm (&o2) != 5)
    return 1;
  Outer o3{{1}};
  if (via_reference_parm (o3) != 5)
    return 1;
  if (via_local () != 5)
    return 1;
  return 0;
}
