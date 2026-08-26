// D4324/P2680: the post<>-only sibling of d4324-conveyor-pre-
// nonconveyor-function-bad.C -- a conveyor-flavored post<> alone (no
// pre<>, no 'conveyor' keyword) must also trigger the implicit
// is_object_address(&x) synthesis for f's own reference parameter.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>

// never_proven_conveyor_v, deliberately: this test is about f's own
// synthesized is_object_address(&x) obligation, not about postcondition
// self-verification (a separate, unrelated concern -- see
// project_eager_instantiation_soundness_bug for that topic).
// never_proven_conveyor_v is still is_conveyor()==true (all that Fix 1's
// own trigger condition needs) but skips the "am I actually true"
// self-check entirely.
int f (int& x) post<std::contracts::never_proven_conveyor_v> (true)
{ return x; }

void h (int* p)
{
  f (*p); // { dg-error "cannot prove .is_object_address. for .\\* p." }
	  // { dg-message "no fact relating this value" "unprovable reason" { target *-*-* } .-1 }
}
