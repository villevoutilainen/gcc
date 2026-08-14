// D4324/P2680 item 8, Increment E1: straight-line propagation through
// a constant shift -- 'j = n + 1' with n's range established as
// [1, 1000000) shifts to j's range [2, 1000001), still excluding zero
// (and, per item 8's own overflow scan, still provably within range).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n) conveyor
{
  if (n > 0 && n < 1000000)
    {
      int j = n + 1;
      return 10 / j;
    }
  return 0;
}

int main () { return f (1) - 5; }
