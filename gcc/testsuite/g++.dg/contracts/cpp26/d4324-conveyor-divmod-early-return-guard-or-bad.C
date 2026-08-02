// D4324/P2680 item 8, Increment J: a guard whose De Morgan's-refined
// range still doesn't exclude zero -- 'n < 0 || n > 1000000000'
// negates to 'n >= 0 && n <= 1000000000', which still permits n == 0
// -- must still be correctly rejected, confirming this is genuine
// interval computation, not a loose heuristic.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n) conveyor
{
  if (n < 0 || n > 1000000000)
    return 0;
  return 10 / n; // { dg-error "divisor .n. not provably nonzero in a conveyor function" }
}

int main () { int x = 1; return f (x) - 10; }
