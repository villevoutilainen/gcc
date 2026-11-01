// D4324: a defaulted virtual destructor that isn't declared 'conveyor'
// isn't conveyor, so conveyor code may not destroy an object of its class
// (nor call it virtually).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct B { virtual ~B () = default; int v = 0; };

int
f () conveyor
{
  B b {};	// { dg-error "not declared .conveyor." }
  return b.v;
}
