// D4324/P2680 item 8, Increment J: the ||-conjunct decomposition gap
// -- the exact originally-cited motivating example now works directly
// via De Morgan's, without needing the two-if workaround
// d4324-conveyor-array-bound-early-return-guard-ok.C used: negating
// 'k < 0 || k >= 5' gives 'k >= 0 && k <= 5', a plain conjunction
// refinable one negated disjunct at a time.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

const int arr[5] = { 10, 11, 12, 13, 14 };

int f (int k) conveyor
{
  if (k < 0 || k >= 5)
    return 0;
  return arr[k];
}

int main () { return f (2) - 12; }
