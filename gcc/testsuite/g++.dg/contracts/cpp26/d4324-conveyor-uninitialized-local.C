// D4324: every local variable in a conveyor function must be explicitly
// initialized at its point of declaration.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor
{
  int y; // { dg-error "local variable .y. not explicitly initialized in a conveyor function or predicate" }
  y = x;
  return y;
}
