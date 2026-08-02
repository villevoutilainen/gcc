// D4324/P2680 item 8, Increment N: the same shape without the
// preceding guard -- i stays unbounded below, must stay rejected,
// confirming the loop-condition scan actually fires (previously this
// array access was never checked at all).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int arr[5];

int f (int i) conveyor
{
  int sum = 0;
  for (; i < 5 && arr[i] >= 0; i++) // { dg-error "array index .i. not provably in-bounds in a conveyor function" }
    sum++;
  return sum;
}

int main () { int x = 0; return f (x); }
