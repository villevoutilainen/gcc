// D4324: a conveyor function's body may not contain a static_cast that
// performs a base-to-derived conversion.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct B { virtual ~B () {} };
struct D : B {};

int f (B& b) conveyor
{
  D& d = static_cast<D&> (b); // { dg-error "static_cast. performing a base-to-derived conversion not permitted in a conveyor function or predicate" }
  return (void) d, 0;
}
