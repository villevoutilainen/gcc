// D4324/P2680, Increment M: same shape as the "every case" test, but
// one case leaves n unprovable -- must stay rejected, confirming the
// merge is a real AND/intersection across cases, not overly
// permissive.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int flag, int junk) conveyor
{
  int n = junk;
  switch (flag)
    {
    case 0:
      n = 5;
      break;
    case 1:
      n = junk;
      break;
    default:
      n = 9;
      break;
    }
  return 10 / n; // { dg-error "divisor .n. not provably nonzero in a conveyor function" }
}

int main () { int x = 1; return f (x, 0); }
