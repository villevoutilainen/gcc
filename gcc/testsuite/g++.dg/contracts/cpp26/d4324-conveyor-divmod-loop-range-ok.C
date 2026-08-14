// D4324/P2680 item 8, Increment E3: a loop-reassigned integer's range
// facts merge by union (the pre-loop value union every self-reference-
// free reassignment) across loop iterations, applied here to a
// counter starting at a provably-nonzero value and only ever
// incremented.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n) conveyor
{
  int last = 0;
  for (int i = 1; i < n; ++i)
    last = 10 / i;
  return last;
}

int main () { return f (5) - 2; }
