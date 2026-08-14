// D4324/P2680 item 8's overflow scan: general binary PLUS_EXPR -- two
// completely unconstrained operands leave 'a + b' unprovable (neither
// the type-bound witness route, which only ever rescues a shift of
// exactly 1, nor a numeric range on either operand, is available
// here), so it's rejected, same as the rest of item 8's "unprovable is
// always an error" discipline.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int a, int b) conveyor
{
  return a + b; // { dg-error "not provably free of overflow in a conveyor function" }
}

int main () { return f (1, 2); }
