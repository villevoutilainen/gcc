// D4324: an object constructed in the return slot -- a returned prvalue,
// or a named local under NRV -- has a valid address, which the GIMPLE
// engine sees as the function's by-reference RESULT_DECL.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }

#include <contracts>

struct A {
  int *p;
  A () conveyor : p (nullptr) {}
  A (const A &o) conveyor : p (o.p) {}
  ~A () conveyor {}
};

A named () { A a; return a; }
A prvalue () { return A (); }
A copied (const A &r)
  pre<std::contracts::conveyor_assert_v> (std::is_object_address (&r))
{ return r; }
A named_conveyor () conveyor { A a{}; return a; }

int main ()
{
  A a = named ();
  A b = prvalue ();
  A c = copied (a);
  A d = named_conveyor ();
  return 0;
}
