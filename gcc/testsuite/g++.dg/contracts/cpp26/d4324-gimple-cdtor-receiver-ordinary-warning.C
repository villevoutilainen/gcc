// D4324: in ordinary code, a constructor's or destructor's receiver
// that can't be shown to be an object address is a proof the analysis
// couldn't make, not a broken rule: a warning.  A non-conveyor call
// handed 'this' may have ended its object, so it no longer proves
// 'this' afterwards.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-analysis-engine=gimple" }

#include <contracts>

struct H { int* p; H (int* q) : p (q) {} ~H () {} };

void kill (H* p)
{
  p->~H (); // { dg-warning "cannot verify .is_object_address. for .p." }
}

struct S
{
  int buf[2];
  H h;
  int* get () { return buf; }
  S () : h (get ()) {} // { dg-warning "cannot verify .is_object_address. for \[^\n\]*required by the precondition of .H::H\\(int\\*\\)." }
  
};

void use () { S s; }
