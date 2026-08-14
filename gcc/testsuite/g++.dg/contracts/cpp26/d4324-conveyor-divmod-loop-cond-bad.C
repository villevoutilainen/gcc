// D4324/P2680 item 8, Increment N: the same shape without the
// preceding guard -- n stays unprovable, must stay rejected,
// confirming the loop-condition scan actually fires (previously this
// division was never checked at all).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n, int i) conveyor
{
  if (i < 0)
    return 0;
  int total = 0;
  for (; i < 10 && 10 / n > 0; i++) // { dg-error "divisor .n. not provably nonzero in a conveyor function" }
    total += i;
  return total;
}

int main () { int x = 1; return f (x, 0); }
