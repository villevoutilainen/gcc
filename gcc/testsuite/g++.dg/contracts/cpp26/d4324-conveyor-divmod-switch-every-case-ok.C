// D4324/P2680, Increment M: every case (including default) sets n to
// a provably-nonzero value, so a division after the switch is
// accepted -- confirms the N-way merge actually unions facts across
// every case, the main new capability this increment adds beyond
// Increment L's own reachability-only version.
// { dg-do run { target c++26 } }
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
      n = 7;
      break;
    default:
      n = 9;
      break;
    }
  return 10 / n;
}

int main () { return f (0, 0) - 2; }
