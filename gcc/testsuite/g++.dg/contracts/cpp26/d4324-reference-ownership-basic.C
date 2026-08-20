// D4324/P2680 Q2 (the cone-of-evaluation ownership rule), RECONSIDERED
// after discussion with the P2680 paper's author: ownership is keyed on
// RECEIVEDNESS, not on static type, but only ever applies to
// REFERENCES. A reference parameter RECEIVED by this function is OWNED
// -- forwarding it non-const to a further conveyor call doesn't extend
// the cone, since the caller already handed it over legitimately; the
// caller (all the way up the chain) is the one responsible for not
// letting it alias something outside the cone in the first place.
//
// POINTERS -- including the implicit 'this' receiver of a member
// conveyor call -- are entirely EXEMPT from this whole analysis, in any
// context: pointer aliasing is categorically the calling function's own
// concern, never the callee's, matching Q1's own pre-existing boundary
// (a pointer never gets an implicit is_object_address obligation
// either). Widening Q2 to cover pointers was tried and reverted: the
// exact gap it aimed to close (a fresh pointer bound to a directly-
// named global) is already fully closed by a wholly separate,
// independent restriction (conveyor code may never odr-use a mutable
// global at all, in any shape), and the widening broke the widely-used
// "named predicate" query pattern throughout this engine (an ordinary,
// read-only, non-const pointer parameter used for querying, not
// mutation) -- see d4324-reference-ownership-member-receiver.C.
// { dg-do compile { target c++26 } }
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

struct T { int v; };

int use_val_mut (T& x) conveyor { return x.v; }
int use_val_const (const T& x) conveyor { return x.v; }

// A REFERENCE parameter, forwarded directly to another conveyor call's
// non-const reference parameter: accepted -- Y was received as this
// function's own parameter, so re-lending it doesn't extend the cone.
int
accept_ref_param (T& y) conveyor
{
  return use_val_mut (y);
}

// A POINTER parameter's own POINTEE, dereferenced back out to another
// conveyor call's non-const REFERENCE parameter: still rejected -- P's
// own storage is this function's private copy, but *P is still the
// caller's own, unrelated object. (This is Q2 restricting the
// REFERENCE-typed target of use_val_mut, not anything about P itself --
// P's own value, used directly, would be entirely unchecked.)
int
reject_ptr_param_dereference (T* p) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  return use_val_mut (*p); // { dg-error "is not owned by the calling function" }
                           // { dg-message "does not name a parameter or local" "unprovable reason" { target *-*-* } .-1 }
}

// A CONST reference parameter re-lent as const: Q2 doesn't apply to
// const references at all, only the (already-satisfied) Q1 obligation
// does.
int
accept_const_ref_param (const T& y) conveyor
{
  return use_val_const (y);
}

int
main ()
{
  T t{1};
  accept_const_ref_param (t);
  accept_ref_param (t);
  return 0;
}
