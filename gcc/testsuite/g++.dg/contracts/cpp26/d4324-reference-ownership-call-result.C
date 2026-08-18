// D4324/P2680 Q2's ownership-laundering fix: a local reference/pointer
// bound from a conveyor call's return value is NOT automatically owned
// just because it's a local of this function -- a conveyor can't
// allocate or produce an address "from thin air", so its return value
// must alias one of the pointer/reference arguments actually passed at
// that call site (or be self-contained, if it took none). identity_view
// below is a trivial pass-through: its return value is exactly as owned
// (or borrowed) as whatever pointer/reference argument it was given.
//
// BORROWED source: dereferencing a by-value pointer parameter, passed
// through a CONST reference parameter -- Q2 never restricts const
// targets, so the call site itself is legal, but oa_call_result_owned_p
// still correctly infers the return as borrowed by inspecting the
// argument's own ownership, independent of the callee's parameter
// constness. (A directly-named global would have been a simpler
// source, but conveyor code may never odr-use a mutable global at all
// -- a separate, stricter, unrelated restriction -- so it can't reach
// this analysis to test it.) A dedicated case below confirms call-
// result inference correctly follows the ownership flip for a RECEIVED
// parameter too (see d4324-reference-ownership-basic.C).
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

T&
identity_view (const T& cr) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (&cr))
  post<conveyor_ctrl_v>(r: std::is_object_address (&r))
{
  return const_cast<T&> (cr);
}

T*
identity_view_ptr (const T& cr) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (&cr))
  post<conveyor_ctrl_v>(r: std::is_object_address (r))
{
  return const_cast<T*> (&cr);
}

// REFERENCE bound from a conveyor call, itself given a BORROWED
// argument (Q's own pointee, dereferenced): the binding local must stay
// borrowed too, not read as owned merely because it's this function's
// own VAR_DECL.
int
reject_ref_from_call (T* q) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (q))
{
  T& view = identity_view (*q);
  return use_val_mut (view); // { dg-error "is not owned by the calling function" }
}

// Same, but through a POINTER bound from the call and dereferenced.
int
reject_ptr_from_call (T* q) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (q))
{
  T* p = identity_view_ptr (*q);
  return use_val_mut (*p); // { dg-error "is not owned by the calling function" }
}

// Two-hop: a REFERENCE bound from dereferencing a POINTER that was
// itself bound from a borrowed call result -- must still reject; the
// pointer's own ownership (borrowed) must be visible through the extra
// hop, not just through a direct call-result binding.
int
reject_two_hop (T* q) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (q))
{
  T* p2 = identity_view_ptr (*q);
  T& r = *p2;
  return use_val_mut (r); // { dg-error "is not owned by the calling function" }
}

// The OWNED counterpart: identity_view's own argument is LOCAL, a
// genuinely owned local of THIS function -- the binding local must be
// accepted as owned, by the same "conveyors can't fabricate an address"
// reasoning.
int
accept_ref_from_call ()
{
  T local{5};
  T& view = identity_view (local);
  return use_val_mut (view);
}

// Same, but through a pointer bound from the call and dereferenced.
int
accept_ptr_from_call ()
{
  T local{5};
  T* p = identity_view_ptr (local);
  return use_val_mut (*p);
}

// A THIRD source of ownership: identity_view's own argument is a
// RECEIVED reference parameter -- owned under the reworked model
// (receivedness itself grants ownership), so call-result inference must
// now correctly follow that through too.
int
accept_ref_from_call_via_received_param (T& y) conveyor
{
  T& view = identity_view (y);
  return use_val_mut (view);
}

int
main ()
{
  accept_ref_from_call ();
  accept_ptr_from_call ();
  return 0;
}
