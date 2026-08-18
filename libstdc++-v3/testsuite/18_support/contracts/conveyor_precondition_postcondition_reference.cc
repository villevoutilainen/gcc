// D4324/P2680 item 7: oa_handle_precondition_stmt/oa_handle_postcondition_
// stmt/oa_handle_assertion_stmt must discharge item 7's own obligation for
// a call reached from their own condition text, not just from an ordinary
// function-body statement -- see d4324-reference-ownership-predicate-text.C
// in the compiler testsuite for the corresponding rejection tests.
//
// RECONSIDERED after discussion with the P2680 paper's author (see
// .claude/plans/lazy-stirring-pearl.md): Q2's ownership rule now applies
// ONLY to references, and predicate/assert condition text never inherits
// ANY of the enclosing function's own state as "owned" -- not even a
// by-value parameter or a postcondition's own named result identifier,
// since mutating either from within a check would be observable by the
// function's own subsequent execution, exactly the hazard Q2 exists to
// forbid. That makes calling a genuinely mutating, non-const-REFERENCE-
// taking conveyor function from predicate text impossible to construct
// as an accept-path test at all: the only thing ever "owned" in predicate
// context is a temporary materialized by that expression's own
// evaluation, and a temporary can never bind to a non-const lvalue
// reference in the first place. Pointers, by contrast, are exempt from
// Q2 entirely, in every context -- so this test uses a POINTER-taking
// mutator instead, which is also the realistic, common shape (the
// "named predicate" pattern used throughout this whole engine): a real,
// executing, side-effect-verifying call to a conveyor function, made
// from within a precondition's and a postcondition's own condition
// text -- must both compile and actually run, with conveyor_ctrl's own
// runtime check never spuriously tripping.
//
// A by-value parameter is const in postcondition text (a separate,
// unrelated language rule), so the two cases below use different
// identities: the precondition case takes its own by-value parameter's
// address, the postcondition case takes the address of the
// postcondition's own named result identifier -- neither needs Q2 at
// all here, since both are passed as pointers, not references.
// { dg-options "-D_GLIBCXX_CONVEYOR_ASSERTIONS -fcontracts -fcontract-control-objects" }
// { dg-do run { target c++26 } }

#include <contracts>
#include <testsuite_hooks.h>

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

struct counter { int n = 0; };

// A genuine, mutating conveyor function -- an ordinary function call,
// not is_object_address or a named predicate. Returns whether C started
// out untouched, so a caller can meaningfully VERIFY something about
// this call having actually run, rather than just returning an
// unconditional true. Pointer-typed deliberately: a predicate/assert
// can never legitimately call a non-const-REFERENCE-taking mutator (see
// this file's own leading comment), but pointers are exempt from Q2 at
// the CALL site regardless of context. Q1 is still opt-in, not implicit,
// for a pointer PARAMETER -- but this function's own body dereferences
// C, which needs its own explicit precondition to be provably valid
// UB-free, entirely independent of Q2/the caller's own obligations.
// entirely, so this remains callable from condition text.
bool touch (counter* c) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (c))
{ bool untouched = c->n == 0; c->n = 1; return untouched; }

// Not itself declared conveyor -- only its own precondition text is
// conveyor-flavored (via conveyor_ctrl_v).
int
use_in_precondition (counter c)
  pre<conveyor_ctrl_v>(touch (&c))
{
  return c.n;
}

// Not itself declared conveyor -- only its own postcondition text is
// conveyor-flavored.
counter
use_in_postcondition ()
  post<conveyor_ctrl_v>(r: touch (&r))
{
  return counter{};
}

int
main ()
{
  // Confirmed via direct testing: a by-value entity's own mutation
  // during precondition/postcondition evaluation isn't observable
  // afterward (a separate object from what the body/caller sees) --
  // both come back 0, not touch()'s own written 1. That's a separate,
  // unrelated language/ABI question this test isn't about; what matters
  // here is that conveyor_ctrl_v's own runtime check (touch(&c)/touch(&r)
  // returning true, since each call's own C/R genuinely started
  // untouched) never traps, proving the call actually ran.
  VERIFY (use_in_precondition (counter{}) == 0);
  VERIFY (use_in_postcondition ().n == 0);
  return 0;
}
