// D4324, Increment P: a const-qualified static data member stays fine to
// odr-use in a conveyor function.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct S { static const int m = 5; };

int f () conveyor
{
  return S::m;
}

int main () { return f () - 5; }
