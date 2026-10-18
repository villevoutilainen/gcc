// D4324: GIMPLE-engine counterpart of
// d4324-conveyor-pre-nonconveyor-function-bad.C: a conveyor
// precondition reading a reference parameter makes the call site owe
// 'is_object_address' for the argument, even for a non-conveyor callee
// -- through the precondition synthesized for the reference ('&x'
// arrives as a conversion of x).  The argument is named by the object
// the reference binds to.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-analysis-engine=gimple" }

#include <contracts>

void f (int& x) pre<std::contracts::conveyor_assert_v> (x < 2048)
{}

void h (int* p)
{
  f (*p); // { dg-error "cannot prove .is_object_address. for .\\* p." }
	  // { dg-message "no fact relating this value" "unprovable reason" { target *-*-* } .-1 }
	  // { dg-warning "cannot verify that .\\* p. satisfies" "" { target *-*-* } .-2 }
}
