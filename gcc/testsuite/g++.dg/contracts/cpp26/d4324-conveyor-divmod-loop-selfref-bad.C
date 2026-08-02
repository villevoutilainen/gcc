// D4324/P2680 item 8, Increment E-divmod: the loop-header merge rule
// must reject a divisor reassignment that circularly depends on its
// own prior value ('b = b + 1;'), the same restriction item 4 already
// enforces for is_object_address.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n) conveyor
{
  int b = 3;
  for (int i = 0; i < n; ++i)
    b = b + 1;
  return 10 / b; // { dg-error "divisor .b. not provably nonzero in a conveyor function" }
}

int main () { int x = 1; return f (x) - 3; }
