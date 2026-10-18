// D4324: with definition-side checking turned off, a definition's strict
// predicate is not woven into its body at all; its restrictions must
// still be checked (at declaration level).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontracts-definition-check=off" }

#include <contracts>

namespace sc = std::contracts;

int defined (int x) pre<sc::conveyor_assert_v> ((x = 3) > 0)	// { dg-error "assignment to .x. not permitted in a conveyor predicate" }
{ return x; }

int post_defined (int& x) post<sc::conveyor_assert_v> (r: (x = r) > 0) // { dg-error "assignment to .x. not permitted in a conveyor predicate" }
{ return 1; }
