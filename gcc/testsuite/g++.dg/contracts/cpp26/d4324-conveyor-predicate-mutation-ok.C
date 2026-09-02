// D4324/P2680: companion to the -bad.C case -- confirms the new
// mandatory "no side effects escaping this condition's own evaluation"
// scan (oa_scan_predicate_mutation_ownership_in_expr) doesn't
// overcorrect into rejecting legitimate cases:
//
// - a temporary the condition's own evaluation materializes may be
//   freely mutated (it's owned by whoever materializes it, same as the
//   existing indirect-mutation Q2 rule already grants);
// - a NON-conveyor predicate (default_control, constify()==false) is
//   entirely untouched by this scan -- mutation there is constify()'s
//   own, existing, unrelated domain;
// - ordinary function-BODY mutation of a conveyor function's own
//   received reference parameter, including forwarding it to another
//   conveyor call, is completely unaffected (this scan never runs
//   outside contract condition text).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct Counter
{
  int v = 0;
  int increment () conveyor { v = 1; return v; }
};

// A local temporary materialized directly by the condition's own
// evaluation ('Counter{}') is owned -- calling a non-const member
// function on it (C++ allows binding a prvalue's own implicit object
// argument to a non-const member function, unlike binding a non-const
// *reference* parameter to one) is fine, the same "owned by whoever
// materializes it" grant the pre-existing ADDR_EXPR(TARGET_EXPR) case
// already gives the indirect (argument-passing) form of this rule.
bool
accept_mutate_condition_temporary ()
  pre<std::contracts::conveyor_assert_v>(Counter{}.increment () == 1)
{
  return true;
}

// No control object named at all -- default_control's own constify()
// is false, so this predicate's own parameter access is left exactly as
// the function body would see it, same as always. This scan has nothing
// to say about non-conveyor condition text either way.
void
accept_plain_pre_mutation (int& x) pre(++x > 0)
{
}

// A conveyor function's own BODY mutating its own received reference
// parameter, and forwarding it to another conveyor call, is completely
// normal and unaffected -- this scan only ever looks at contract
// condition text, never body code.
void
mutate_own_param (int& x) conveyor
  pre<std::contracts::conveyor_assert_v>(x < 100)
{
  ++x;
}

void
forwards_owned_param () conveyor
{
  int local = 0;
  mutate_own_param (local);
}

int
main ()
{
  int y = 0;
  accept_plain_pre_mutation (y);
  if (y != 1)
    return 1;
  forwards_owned_param ();
  return accept_mutate_condition_temporary () ? 0 : 1;
}
