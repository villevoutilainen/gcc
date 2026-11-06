// D4324: binding a reference to a contracted function pointer is like
// copying the pointer: calls through the reference aren't checked against
// the pointer's contract, so -Wfunction-pointer-contract-mismatch warns
// about the binding (P4445 BUG-07).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -Wfunction-pointer-contract-mismatch" }

#include <contracts>

void f (int) {}
void (*p) (int a) pre (a > 0) = f;
void (*u) (int) = f;

void take (void (*&r) (int));

struct M { void m (int); };
void (M::*pm) (int a) pre (a > 0) = &M::m;

void g ()
{
  void (*&r) (int) = p;		// { dg-warning "binding reference 'r', which has no contract, to 'p'" }
  void (*&s) (int) = u;		// no contract on u: no warning
  take (p);			// { dg-warning "binding reference 'r', which has no contract, to 'p'" }
  void (M::*&rm) (int) = pm;	// { dg-warning "binding reference 'rm', which has no contract, to 'pm'" }
  void (* const &cr) (int) = p;	// { dg-warning "binding reference 'cr', which has no contract, to 'p'" }
  (void) r; (void) s; (void) rm; (void) cr;
}
