// D4324, Increment P: a const-qualified namespace-scope variable stays
// fine to odr-use in a conveyor function.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

const int g = 5;

int f () conveyor
{
  return g;
}

int main () { return f () - 5; }
