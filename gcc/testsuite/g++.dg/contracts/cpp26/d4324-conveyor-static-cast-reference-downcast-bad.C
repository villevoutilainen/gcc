// D4324, Increment R: symmetry check alongside the new pointer-downcast
// test -- the reference form of a base-to-derived static_cast was
// already restricted before this increment; confirm it still is.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct Base { virtual ~Base () {} };
struct Derived : Base { int v; };

int f (Base& b) conveyor
{
  Derived& d = static_cast<Derived&> (b); // { dg-error ".static_cast. performing a base-to-derived conversion not permitted in a conveyor function or predicate" }
  return d.v;
}
