// D4324/P2680 item 8's overflow scan: general binary MINUS_EXPR -- two
// completely unconstrained operands leave 'a - b' unprovable.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int a, int b) conveyor
{
  return a - b; // { dg-error "not provably free of overflow in a conveyor function" }
}

int main () { return f (1, 2); }
