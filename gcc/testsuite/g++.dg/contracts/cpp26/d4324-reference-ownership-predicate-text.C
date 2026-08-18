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

int
main ()
{
  T t{1};
  accept_const_in_precondition (t); // { dg-warning "cannot verify that" }
  return 0;
}
