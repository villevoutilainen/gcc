// D4324/P2680: std::is_object_address has no definition and can never
// be evaluated at runtime, so it is ill-formed anywhere outside a
// conveyor- or symbolic-checked contract_assert/pre/post condition --
// checked explicitly at the point of use, not assumed from context, and
// regardless of whether the argument would otherwise be trivially
// provable.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int f ()
{
  int x = 5;
  return std::is_object_address(&x); // { dg-error "may only be used directly inside a conveyor- or symbolic-checked" }
}

int main () { return f (); }
