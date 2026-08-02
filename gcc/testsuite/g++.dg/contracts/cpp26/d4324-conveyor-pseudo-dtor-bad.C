// D4324, Increment Q: a pseudo-destructor call (a destructor spelling
// on a scalar/non-class type, which does nothing but is syntactically
// valid) is not permitted in a conveyor function.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

typedef int I;

int f (int* p) conveyor
{
  p->I::~I (); // { dg-error "pseudo-destructor call not permitted in a conveyor function or predicate" }
  return 0;
}
