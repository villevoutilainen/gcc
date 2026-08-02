// D4324/P2680, Increment M: the soundness-trap shape itself -- 'case
// 0: n = 5; case 1: return 10 / n;' (intentional fallthrough) must
// stay rejected. A switch has multiple valid entry points, and case 1
// can be entered *directly*, bypassing case 0's assignment entirely
// -- so n's fact must be reset at every case/default label, never
// assumed to carry across a label boundary via fallthrough. This is
// the test that would have been unsound without that reset.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int flag, int junk) conveyor
{
  int n = junk;
  switch (flag)
    {
    case 0:
      n = 5;
    case 1:
      return 10 / n; // { dg-error "divisor .n. not provably nonzero in a conveyor function" }
    default:
      return 0;
    }
}

int main () { int x = 1; return f (x, 0); }
