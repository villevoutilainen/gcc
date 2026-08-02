// D4324/P2680 item 8, Increment K: the div/mod analogue -- a later
// conjunct's division benefits from an earlier conjunct's bare
// 'n != 0' nonzero-ness fact, within the very same '&&'-chain. This
// also exercises the fix's own nz-conjunct seeding
// (oa_nonzero_conjunct_p, since oa_refine_single_comparison alone
// doesn't handle '!=' at all).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n, int m) conveyor
{
  if (n != 0 && 10 / n > m)
    return 1;
  return 0;
}

int main () { return f (5, 0) - 1; }
