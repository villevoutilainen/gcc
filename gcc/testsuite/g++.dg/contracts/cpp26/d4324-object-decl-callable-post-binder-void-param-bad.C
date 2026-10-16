// D4324: a single-name postcondition binder names the parameter of a
// void callable only when there is exactly one parameter; otherwise it
// is still a result name, which a void callable doesn't have.  Ordinary
// function declarations are unaffected (see dcl.contract.res.p1.C).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

void two (int, int) { }
void none () { }

void (*fp2) (int, int) post<> (x: x > 0) = two;
void (*fp0) () post<> (x: x > 0) = none;
// Two names on a one-parameter void callable: result, then parameter.
void (*fpr) (int) post<> (r, x: x > 0);

void f (const int x) post (r: x > 0); // { dg-error "function does not return a value to test" }

void
use ()
{
  fp2 (1, 2);	// { dg-error "function does not return a value to test" "" { target *-*-* } 13 }
  fp0 ();	// { dg-error "function does not return a value to test" "" { target *-*-* } 14 }
  fpr (1);	// { dg-error "function does not return a value to test" "" { target *-*-* } 16 }
}
