// D4324, Increment Q: an explicit destructor call on a class type
// (as opposed to an implicit one at scope exit) is not permitted in a
// conveyor function.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct S { int v; ~S () {} };

int f (S& obj) conveyor
{
  obj.~S (); // { dg-error "explicit destructor call not permitted in a conveyor function or predicate" }
  return 0;
}
