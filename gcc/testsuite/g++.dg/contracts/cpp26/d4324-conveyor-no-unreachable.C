// D4324: a conveyor function's body may not call std::unreachable.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <utility>

int f (int x) conveyor
{
  if (x > 0)
    return x;
  std::unreachable (); // { dg-error "std::unreachable. not permitted in a conveyor function or predicate" }
}
