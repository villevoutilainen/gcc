// D4324/P2680: a conveyor-flavored contract condition (pre<>/post<>/
// contract_assert<>) must not have side effects that escape its own
// evaluation -- a direct assignment or increment/decrement of anything
// the condition's own evaluation didn't itself create (a parameter of
// the enclosing function, 'this', a global -- anything merely VISIBLE
// to the condition, not RECEIVED by it) is a mandatory error, the same
// "always checked, never opt-in" character as item 8's own scans.
//
// This is the direct-mutation companion to d4324-reference-ownership-
// predicate-text.C, which already covers the *indirect* case (passing
// such a target by non-const reference to another conveyor call). Found
// via direct testing (Ville, via a Godbolt repro): 'pre<conveyor_
// assert_v>((x = 5, true))' compiled with no diagnostic at all before
// this fix, for both a reference and a by-value parameter.
//
// Deliberately scoped to CONVEYOR-flavored condition text only -- see
// the -ok.C companion for why a non-conveyor predicate (default_control,
// constify()==false) is untouched by this and stays accepted.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

// Reference parameter, direct assignment, in a precondition.
void
reject_assign_reference_precondition (int& x)
  pre<std::contracts::conveyor_assert_v>((x = 5, true)) // { dg-error "assignment to .x. not permitted in a conveyor predicate" }
{
}

// By-value parameter, direct assignment, in a precondition -- the
// enclosing function needn't itself be conveyor for this to apply.
void
reject_assign_by_value_precondition (int x)
  pre<std::contracts::conveyor_assert_v>((x = 0, true)) // { dg-error "assignment to .x. not permitted in a conveyor predicate" }
{
}

// Reference parameter, direct assignment, in contract_assert. Needs its
// own preceding is_object_address self-trust (never automatically
// synthesized for a bare contract_assert the way it is for pre<>/
// post<>) purely so item 8's own, unrelated dereference-validity check
// doesn't also fire here -- this test isolates the ownership check.
void
reject_assign_reference_assert (int& x)
  pre<std::contracts::never_proven_conveyor_v>(std::is_object_address (&x))
{
  contract_assert<std::contracts::conveyor_assert_v>((x = 0, true)); // { dg-error "assignment to .x. not permitted in a conveyor predicate" }
}

// Increment/decrement are covered too, not just plain assignment --
// deliberately bounded first ('x < 100') so item 8's own, entirely
// separate overflow-safety scan doesn't also fire on the same
// expression; this test isolates the ownership check specifically.
void
reject_increment_reference_precondition (int& x)
  pre<std::contracts::conveyor_assert_v>(x < 100 && ++x > 0) // { dg-error "increment of .x. not permitted in a conveyor predicate" }
{
}

// Reference parameter, direct assignment, in a postcondition -- the
// same rule applies to post<> as to pre<>/contract_assert<>.
int
reject_assign_reference_postcondition (int& x)
  post<std::contracts::conveyor_assert_v>(r: (x = 0, r >= 0)) // { dg-error "assignment to .x. not permitted in a conveyor predicate" }
{
  return 0;
}

// A conveyor-DECLARED function's own predicate mutating its OWN
// (received) reference parameter is rejected identically -- ownership
// of the enclosing function's own parameters is a body-level grant that
// never extends into predicate/assert text, regardless of whether that
// enclosing function is itself conveyor.
void
reject_even_when_enclosing_function_is_conveyor (int& x) conveyor
  pre<std::contracts::conveyor_assert_v>((x = 5, true)) // { dg-error "assignment to .x. not permitted in a conveyor predicate" }
{
}

int main () { return 0; }
