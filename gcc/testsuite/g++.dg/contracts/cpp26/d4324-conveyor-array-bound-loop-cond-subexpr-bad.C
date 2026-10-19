// D4324/P2680 item 8, Increment N: the same shape without the
// preceding guard -- i stays unbounded below, must stay rejected,
// confirming the loop-condition scan actually fires (previously this
// array access was never checked at all).
// The loop body assigns rather than accumulates: an accumulation is
// bounded only by counting iterations, which neither engine does (the
// AST engine used to check the body with its first iteration's facts).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

const int arr[5] = {};

int f (int i) conveyor
{
  int sum = 0;
  for (; i < 5 && arr[i] >= 0; i++) // { dg-error "array index .i. not provably in-bounds in a conveyor function" }
    sum = 1;
  return sum;
}

int main () { int x = 0; return f (x); }
