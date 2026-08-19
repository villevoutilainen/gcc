// D4324/P2680 item 7's 'this' exception, ported to the built-in
// GIMPLE-pass engine (cg_provable_object_address_p's and cg_provably_
// owned_p's own is_this_parameter checks): re-lending 'this' further --
// even dereferenced, for another conveyor call's non-const reference
// parameter -- never extends the cone of evaluation, since no new party
// gains access; it's still the exact object this function was already
// given. Gated by -fcontract-conveyor-proofs-gimple, run alongside the
// ordinary mandatory AST-level check -- both must independently accept
// this.
//
// Deliberately no base class here: '*this' in a member of a class WITH
// a base class needs an implicit upcast, which GCC represents the same
// way as an ordinary field access ('ADDR_EXPR (COMPONENT_REF (...)))')
// -- cg_provably_owned_p's own "field of an owned object" gap (already
// documented as a deferred follow-up, mirroring oa_reference_owned_p's
// own later, separate refinement) also covers this shape; confirmed by
// direct testing. Not exercised here -- see cg_provably_owned_p's own
// comment for the full account.
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

struct T {
  int v;
  int use_this () conveyor { return use_val_mut (*this); }
  static int use_val_mut (T& x) conveyor { x.v = 5; return x.v; }
};

int main ()
{
  T t{1};
  return t.use_this () - 5;
}
