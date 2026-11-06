// D4324/P2680 item 7: oa_handle_precondition_stmt/oa_handle_postcondition_
// stmt/oa_handle_assertion_stmt must discharge item 7's own obligation for
// a call reached from their own condition text, not just from an ordinary
// function-body statement -- see d4324-reference-ownership-predicate-text.C
// in the compiler testsuite for the corresponding rejection tests.
//
// Condition text owns nothing it didn't create -- not even a by-value
// parameter or a postcondition's named result -- so it may pass a pointer
// to one only to a pointer-to-const parameter: a conveyor function writes
// through const access only to an object that began its lifetime in it
// (a pointer to non-const is rejected; see the compiler test).  This test
// makes a real, executing call to such a conveyor function from a
// precondition's and a postcondition's condition text: it must compile and
// run, with conveyor_ctrl's run-time check never tripping.
//
// A by-value parameter is const in postcondition text (a separate,
// unrelated language rule), so the two cases below use different
// identities: the precondition case takes its own by-value parameter's
// address, the postcondition case takes the address of the
// postcondition's own named result identifier.
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

// A conveyor function an ordinary call reaches from condition text, given
// a pointer to const.  Returns whether C is untouched, so the caller's
// check verifies that the call ran.  Its body dereferences C, which needs
// its own precondition to be provably valid.
bool untouched (const counter* c) conveyor
  pre<conveyor_ctrl_v>(std::is_object_address (c))
{ return c->n == 0; }

// Not itself declared conveyor -- only its own precondition text is
// conveyor-flavored (via conveyor_ctrl_v).
int
use_in_precondition (counter c)
  pre<conveyor_ctrl_v>(untouched (&c))
{
  return c.n;
}

// Not itself declared conveyor -- only its own postcondition text is
// conveyor-flavored.
counter
use_in_postcondition ()
  post<conveyor_ctrl_v>(r: untouched (&r))
{
  return counter{};
}

int
main ()
{
  // conveyor_ctrl_v's run-time check (untouched (&c), untouched (&r))
  // never traps.
  VERIFY (use_in_precondition (counter{}) == 0);
  VERIFY (use_in_postcondition ().n == 0);
  return 0;
}
