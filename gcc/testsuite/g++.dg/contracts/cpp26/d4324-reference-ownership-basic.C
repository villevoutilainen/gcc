// D4324/P2680 Q2 (the cone-of-evaluation ownership rule), RECONSIDERED
// after discussion with the P2680 paper's author: ownership is keyed on
// RECEIVEDNESS, not on static type -- a reference OR pointer parameter
// RECEIVED by this function is OWNED (once its own is_object_address is
// proven, for a pointer): forwarding it, or dereferencing it, non-const
// to a further conveyor call doesn't extend the cone, since the caller
// already handed it over legitimately; the caller (all the way up the
// chain) is the one responsible for not letting it alias something
// outside the cone in the first place.
//
// POINTERS used AS POINTERS -- i.e. as the receiver of a pointer-typed
// callee parameter, including the implicit 'this' receiver of a member
// conveyor call -- remain entirely EXEMPT from Q2, in any context:
// pointer aliasing is categorically the calling function's own concern,
// never the callee's, matching Q1's own pre-existing boundary (a
// pointer never gets an implicit is_object_address obligation either).
// Widening Q2 to cover THIS case (making a pointer-TYPED CALLEE
// PARAMETER itself subject to Q2) was tried and reverted: the exact gap
// it aimed to close (a fresh pointer bound to a directly-named global)
// is already fully closed by a wholly separate, independent restriction
// (conveyor code may never odr-use a mutable global at all, in any
// shape), and the widening broke the widely-used "named predicate"
// query pattern throughout this engine (an ordinary, read-only,
// non-const pointer parameter used for querying, not mutation) -- see
// d4324-reference-ownership-member-receiver.C.
//
// D4324/P2680 (2026-10-15): a DIFFERENT, narrower question -- whether a
// received pointer's own POINTEE counts as owned once DEREFERENCED and
// handed to a non-const REFERENCE-typed callee parameter (Q2 is already
// triggered either way by that reference-typed target; this is purely
// about what counts as owned once it has been) -- used to answer "no"
// (reject_ptr_param_dereference below), even though direct mutation
// through that exact same, already-proven pointer ('p->v = 5;') was
// already unconditionally legal with zero Q2 involvement at all. That
// answer is now "yes": the dereference grants no capability beyond what
// was already, unconditionally available directly, so restricting it
// provided no actual protection -- only friction, forcing an extra
// level of indirection (bind a local reference/pointer first) to reach
// an identical, already-available capability. Confirmed via direct
// testing and an independent red-team pass: no soundness counterexample
// found for this narrower change, and the earlier revert's own stated
// reasons (above) are specific to the broader, callee-parameter-type
// widening and do not apply here -- that one is untouched by this
// change.
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
// conveyor call's non-const REFERENCE parameter: now accepted -- once
// P's own is_object_address is proven, *P is exactly as owned as P's
// own value already was.
int
accept_ptr_param_dereference (T* p) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (p))
{
  return use_val_mut (*p);
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
  accept_ptr_param_dereference (&t);
  return 0;
}
