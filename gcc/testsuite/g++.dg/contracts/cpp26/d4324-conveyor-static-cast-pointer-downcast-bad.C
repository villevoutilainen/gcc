// D4324, Increment R: a static_cast performing a base-to-derived
// (downcast) conversion through a pointer is not permitted in a
// conveyor function -- previously only the reference form of this
// same cast was checked.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct Base { virtual ~Base () {} };
struct Derived : Base { int v; };

int f (Base* b) conveyor
{
  Derived* d = static_cast<Derived*> (b); // { dg-error ".static_cast. performing a base-to-derived conversion not permitted in a conveyor function or predicate" }
  return d ? 1 : 0;
}
