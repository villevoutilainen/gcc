// D4324/P2680 Q2 (the cone-of-evaluation ownership rule): Q2 exists to
// stop a conveyor callee or conveyor predicate -- which by definition
// gets no runtime check -- from silently laundering access to a
// reference it wasn't given standing to touch. That hazard is specific
// to the "no runtime check" zone itself: an ORDINARY, non-conveyor
// caller handing a reference to a conveyor function is an ordinary
// aliasing question like any other function call, catchable by
// ordinary means. So Q2's own diagnostic must be gated on the CALLING
// context itself being conveyor-active -- either the enclosing function
// is itself 'conveyor', or the call is reached from conveyor-flavored
// predicate/assert condition text -- matching item 8's own "functions
// AND predicates" scope, never "any caller of a conveyor callee at
// all". Found via a real false positive: unique_ptr's own (ordinary,
// non-conveyor) destructor calling std::move on a reference to its own
// already-owned 'this' state, reached through a non-conveyor accessor
// (bits/unique_ptr.h's _M_ptr()) -- incorrectly rejected before this
// fix purely because std::move happens to be conveyor-declared.
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

// Const-parameter identity view, matching d4324-reference-ownership-
// call-result.C's own helper exactly: Q2 never restricts a CONST
// target, so a call site passing a borrowed *q here is Q1-clean; the
// const_cast'd return is what later trips Q2 when handed to a non-const
// reference parameter (oa_call_result_owned_p inspecting the call's own
// pointer/reference arguments, q's pointee among them, and finding it
// unowned).
T&
identity_view (const T& cr) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (&cr))
  post<conveyor_ctrl_v>(r: std::is_object_address (&r))
{
  return const_cast<T&> (cr);
}

// An ORDINARY (non-conveyor) accessor -- like bits/unique_ptr.h's own
// _M_ptr() -- with an is_object_address pre/post pair but no 'conveyor'
// keyword: exactly the shape oa_call_result_owned_p can never trace
// (it hard-requires DECL_DECLARED_CONVEYOR_P), so its return has always
// been, and still is, unprovable as OWNED by Q2's own narrow tracing.
// The fix isn't making this call result traceable -- it's recognizing
// that an ORDINARY caller was never in Q2's jurisdiction to begin with.
T&
get_ref (T& t)
  pre<sc::never_proven_conveyor_v>(std::is_object_address (&t))
  post<sc::never_proven_conveyor_v>(r: std::is_object_address (&r))
{
  return t;
}

// The unique_ptr::~unique_ptr() shape: an ORDINARY, non-conveyor
// function handing a reference sourced from an ordinary accessor call
// to a conveyor callee's non-const reference parameter. Accepted --
// this calling function never opted into conveyor's "no runtime check"
// world, so Q2 has nothing to protect here (the caller-owns-it
// responsibility is entirely on this ordinary function, like any other
// aliasing question).
int
accept_ownership_non_conveyor_caller (T& t)
  pre<sc::never_proven_conveyor_v>(std::is_object_address (&t))
{
  T& view = get_ref (t);
  return use_val_mut (view);
}

// Same shape, but the CALLING function is itself 'conveyor': Q2 must
// still reject a borrowed source -- the fix narrows *scope*, not the
// rule itself. (Uses identity_view, a genuinely conveyor callee, since
// an ordinary accessor like get_ref can't even be called from here --
// a conveyor function may only call other conveyor functions.)
int
reject_ownership_still_enforced_in_conveyor_caller (T* q) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (q))
{
  T& view = identity_view (*q);
  return use_val_mut (view); // { dg-error "is not owned by the calling function" }
}

// Same shape, but the violating call is reached from conveyor-flavored
// PREDICATE text of an otherwise ordinary, non-conveyor function: Q2
// must still reject it too, matching item 8's "functions AND
// predicates" scope exactly (this is the predicate half).
int
reject_ownership_still_enforced_in_predicate_text (T* q)
  pre<conveyor_ctrl_v>(std::is_object_address (q))
  pre<conveyor_ctrl_v>(use_val_mut (identity_view (*q)) >= 0) // { dg-error "is not owned by the calling function" }
{
  return 0;
}

int
main ()
{
  T t{1};
  accept_ownership_non_conveyor_caller (t);
  return 0;
}
