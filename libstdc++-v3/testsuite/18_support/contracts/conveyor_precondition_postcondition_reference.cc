// D4324/P2680 item 7: oa_handle_precondition_stmt/oa_handle_postcondition_
// stmt/oa_handle_assertion_stmt must discharge item 7's own obligation (a
// conveyor callee's implicit reference-parameter Q1 obligation, and Q2's
// ownership check for a non-const one) for a call reached from their own
// condition text, not just from an ordinary function-body statement --
// see d4324-reference-ownership-predicate-text.C in the compiler
// testsuite for the corresponding rejection tests (a borrowed reference
// re-lent this way). The real-library conveyor sweep's own precondition
// usage (_GLIBCXX_PRECONDITION_SUBSCRIPT and friends) never actually
// calls a reference-taking function, and its only postcondition
// (__possibly_const_range's) only ever calls is_object_address itself
// (explicitly exempt from this scan) -- so neither exercises this
// mechanism with a genuine call anywhere in real library code. This is
// that missing accept-path test: a genuine, executing call to a
// reference-taking conveyor function, made from within a precondition's
// and a postcondition's own condition text, on an owned identity -- must
// both compile and actually run, with conveyor_ctrl's own runtime check
// never spuriously tripping.
//
// A by-value parameter is const in postcondition text (a separate,
// unrelated language rule), so the two cases below use different owned
// identities: the precondition case uses its own by-value parameter, the
// postcondition case uses the postcondition's own named result
// identifier -- both are this function's own, per Q2's ownership rule.
// Confirmed via direct testing that a by-value entity's own mutation
// during precondition/postcondition evaluation is not observable outside
// that evaluation (a separate object from what the body/caller sees), so
// the two functions below deliberately don't try to observe touch()'s
// own side effect afterward -- only that calling it compiles and runs
// clean is what this test is actually about.
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

// A genuine, non-const reference-taking conveyor function -- an
// ordinary function call, not is_object_address or a named predicate.
// Returns whether C started out untouched, so a caller can meaningfully
// VERIFY something about this call having actually run, rather than
// just returning an unconditional true.
bool touch (counter& c) conveyor { bool untouched = c.n == 0; c.n = 1; return untouched; }

// Not itself declared conveyor -- only its own precondition text is
// conveyor-flavored (via conveyor_ctrl_v). C is a BY-VALUE parameter, so
// it's owned (this function's own independent copy), satisfying Q2 for
// touch(c)'s non-const reference parameter.
int
use_in_precondition (counter c)
  pre<conveyor_ctrl_v>(touch (c))
{
  return c.n;
}

// Not itself declared conveyor -- only its own postcondition text is
// conveyor-flavored. R, the postcondition's own named result identifier,
// is this function's own produced object, satisfying Q2 the same way.
counter
use_in_postcondition ()
  post<conveyor_ctrl_v>(r: touch (r))
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
  // here is that conveyor_ctrl_v's own runtime check (touch(c)/touch(r)
  // returning true, since each call's own C/R genuinely started
  // untouched) never traps, proving the call actually ran.
  VERIFY (use_in_precondition (counter{}) == 0);
  VERIFY (use_in_postcondition ().n == 0);
  return 0;
}
