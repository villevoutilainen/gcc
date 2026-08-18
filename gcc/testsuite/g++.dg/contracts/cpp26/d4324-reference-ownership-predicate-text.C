// D4324/P2680 item 7 must discharge its obligation (a conveyor callee's
// implicit reference-parameter Q1 obligation, and Q2's ownership check
// for a non-const one) for a call reached from a PRECONDITION's,
// POSTCONDITION's, or contract_assert's own condition TEXT, not just
// from an ordinary function-body statement -- previously it was only
// ever discharged for the latter: oa_handle_precondition_stmt/oa_handle_
// postcondition_stmt/oa_handle_assertion_stmt only ever pattern-matched
// specific conjunct shapes (is_object_address(E), a named predicate,
// E != 0), never scanned for an arbitrary call the way an ordinary
// if/loop condition already does. An otherwise-ordinary (non-conveyor)
// function's own 'pre<ctrl>(use_val_mut (y))', y a borrowed reference
// parameter, compiled clean with no ownership error at all before this
// fix, found by direct testing.
//
// RECONSIDERED after discussion with the P2680 paper's author (see
// d4324-reference-ownership-basic.C): a RECEIVED reference parameter is
// now owned from the enclosing FUNCTION's own perspective, so Y below
// is no longer borrowed "everywhere" the way it used to be -- but it is
// still not owned specifically from a PREDICATE's/assert's perspective,
// which never received Y as its own, only has visibility into the
// enclosing function's. So these three rejections below still hold, now
// for that more specific reason. See the asymmetry-demonstrating
// functions further down for the explicit contrast against the same
// parameter's own function body -- and, separately, for why POINTERS
// have no such asymmetry at all (they're exempt from Q2 everywhere).
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

bool use_val_mut (T& x) conveyor { return x.v > 0; }
bool use_val_const (const T& x) conveyor { return x.v > 0; }

// NONE of these three enclosing functions are themselves declared
// conveyor -- only their own condition text is conveyor-flavored (via
// conveyor_ctrl_v), exactly the "conveyor-flavored predicate text on an
// otherwise-ordinary function" scope this fix covers.

int
reject_in_precondition (T& y)
  pre<conveyor_ctrl_v>(std::is_object_address (&y))
  pre<conveyor_ctrl_v>(use_val_mut (y)) // { dg-error "is not owned by the calling function" }
{
  return 0;
}

int
reject_in_postcondition (T& y)
  pre<conveyor_ctrl_v>(std::is_object_address (&y))
  post<conveyor_ctrl_v>(r: use_val_mut (y) && r >= 0) // { dg-error "is not owned by the calling function" }
                                                       // { dg-warning "cannot verify postcondition condition" "" { target *-*-* } .-1 }
{
  return 0;
}

int
reject_in_assertion (T& y)
  pre<conveyor_ctrl_v>(std::is_object_address (&y))
{
  contract_assert<conveyor_ctrl_v>(use_val_mut (y)); // { dg-error "is not owned by the calling function" }
                                                      // { dg-warning "cannot verify .contract_assert. condition" "" { target *-*-* } .-1 }
  return 0;
}

// A CONST reference re-lent as const from within precondition text: Q2
// doesn't apply to const references at all, so this must still compile
// clean -- confirms the new scan doesn't introduce a false positive.
int
accept_const_in_precondition (const T& y)
  pre<conveyor_ctrl_v>(std::is_object_address (&y))
  pre<conveyor_ctrl_v>(use_val_const (y))
{
  return 0;
}

int use_ptr_mut (T* q) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (q))
{ return q->v > 0; }

// The ASYMMETRY, explicit: the same REFERENCE parameter Y is owned
// (forwardable) from the function's own BODY, but NOT owned from its
// PRECONDITION's condition text -- a predicate never received Y as its
// own, it only has visibility into the enclosing function's.
int
accept_in_body_reject_in_precondition (T& y)
  pre<conveyor_ctrl_v>(std::is_object_address (&y))
  pre<conveyor_ctrl_v>(use_val_mut (y)) // { dg-error "is not owned by the calling function" }
{
  return use_val_mut (y); // accepted: the function's own body may forward its own received parameter
}

// POINTERS have NO such asymmetry: pointer aliasing is categorically
// the calling function's own concern, never the callee's, in EITHER
// context -- accepted both in the function's own body and in its
// precondition's condition text (unlike the reference case just above).
// A widened Q2 that also restricted pointers here was tried and
// reverted -- see d4324-reference-ownership-member-receiver.C for why.
int
accept_ptr_in_body_and_in_precondition (T* p)
  pre<conveyor_ctrl_v>(std::is_object_address (p))
  pre<conveyor_ctrl_v>(use_ptr_mut (p))
{
  return use_ptr_mut (p);
}

int
main ()
{
  T t{1};
  accept_const_in_precondition (t); // { dg-warning "cannot verify that" }
  accept_ptr_in_body_and_in_precondition (&t);
  return 0;
}
