// D4324/P2680 item 8, Increment H: the array-bound analogue of the
// early-return-guard fix -- two sequential single-comparison guards
// (deliberately not a single compound '||' guard, since
// oa_collect_conjuncts only decomposes '&&': that's a separate,
// still-open gap, not what this test is about) establish k's range
// for the array access after both ifs.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

const int arr[5] = { 10, 11, 12, 13, 14 };

int f (int k) conveyor
{
  if (k < 0)
    return 0;
  if (k >= 5)
    return 0;
  return arr[k];
}

int main () { return f (2) - 12; }
