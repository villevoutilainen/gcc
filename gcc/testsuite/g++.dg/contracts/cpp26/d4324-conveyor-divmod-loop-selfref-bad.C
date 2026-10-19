// D4324/P2680 item 8, Increment E-divmod: a divisor reassigned in a loop
// from its own prior value is known after the loop only by what holds
// on every iteration -- 'b = b - 1;' can reach zero.  ('b = b + 1;'
// keeps 'b >= 3': what the loop head's fixed point now finds.)
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n) conveyor
{
  int b = 3;
  for (int i = 0; i < n; ++i)
    b = b - 1; // { dg-error "(result of .\\(b - 1\\).|decrement of .b.) not provably free of overflow" }
  return 10 / b; // { dg-error "divisor .b. not provably nonzero in a conveyor function" }
}

int main () { int x = 1; return f (x) - 3; }
