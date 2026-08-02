// D4324/P2680, Increment M: closes the "no SWITCH_STMT case at all"
// gap in oa_walk_stmt -- a fact established and consumed within the
// same case's own run is now correctly tracked, confirming ordinary
// fact-tracking happens inside a switch body at all.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int flag) conveyor
{
  int n = 1;
  switch (flag)
    {
    case 0:
      n = 5;
      return 10 / n;
    default:
      return 0;
    }
}

int main () { return f (0) - 2; }
