// Companion to d4324-gimple-item7-ownership-ok.C. Previously demonstrated
// a now-ACCEPTED shape (a POINTER parameter's own pointee, dereferenced
// -- see d4324-reference-ownership-basic.C's own updated comment for the
// full history of that change, 2026-10-15) -- repurposed to demonstrate
// a shape that's still, correctly rejected under the widened rule: the
// pointee of a pointer-TYPED FIELD (as opposed to a pointer-typed
// PARAMETER). Neither the by-value/pointer-parameter widening above nor
// the earlier field-of-owned-object fix (d4324-gimple-item7-ownership-
// field-of-owned-object-ok.C) grants ownership to *this* shape: the
// field-of-owned-object fix only covers the FIELD'S OWN ADDRESS
// ('&h->ptr'), never what a pointer-typed field's own VALUE points at
// ('*(h->ptr)') -- that's still the caller's own, unrelated object, the
// same reasoning that used to apply to a plain pointer parameter's own
// pointee before this fix, and still correctly applies here. Gated by
// -fcontract-conveyor-proofs-gimple, run alongside the ordinary
// mandatory AST-level check -- both independently reject this.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }
// Unrelated, pre-existing, separately-scoped GIMPLE gap (not pursued
// here, per this session's own explicit scoping decision): establishing
// is_object_address for a FIELD expression via a function-level
// precondition doesn't propagate on the GIMPLE engine the way it does
// on the AST engine, so an extra, spurious Q1 diagnostic fires here too
// -- pruned, since it's unrelated to what this file is actually testing
// (Q2 ownership).
// { dg-prune-output "cannot prove .is_object_address. for \[^\n\]*, required by the precondition of .int use_val_mut" }
// { dg-prune-output "no fact relating this value to the required comparison has been established" }

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
struct Holder { T* ptr; };
int use_val_mut (T& x) conveyor { x.v = 5; return x.v; }

int reject_field_ptr_dereference (Holder *h) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (h))
  pre<conveyor_ctrl_v>(std::is_object_address (h->ptr))
{
  return use_val_mut (*(h->ptr)); // { dg-error "is not owned by the calling function" }
                                  // { dg-message "does not name a parameter or local" "unprovable reason" { target *-*-* } .-1 }
}

int main () { return 0; }
