// D4324: the ABI's own pointers to a virtual base are valid in a conveyor
// constructor of a class with one, but nothing else is: a user-written
// conveyor constructor's own dereference of an unproven pointer still
// errors, in each variant of the constructor; and so does reaching the
// virtual base of an object that isn't proven to be one.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>

struct B { int x = 0; };
struct U : virtual B
{
  int *p;
  U (int *q) conveyor : p (q) { *p = x; }	// { dg-error "pointer dereference" }
};
struct T : virtual B { virtual void f () = 0; T (int *q) conveyor { x = *q; } };	// { dg-error "pointer dereference" }
struct S : virtual B
{
  int y;
  S (const S *o) conveyor : y (o->x) {}	// { dg-error "pointer dereference|is_object_address" }
};
