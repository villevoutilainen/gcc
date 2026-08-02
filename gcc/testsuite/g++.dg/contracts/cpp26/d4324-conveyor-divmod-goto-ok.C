// D4324/P2680 item 8, Increment L: a bare 'goto label;' is recognized
// by oa_stmt_terminates_p the same way -- it never falls through to
// whatever textually follows it, so the merge right after the if
// correctly uses only the else-branch's facts.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n, int junk) conveyor
{
  int m = junk;
  if (n <= 0)
    goto skip;
  else
    m = 5;
  if (10 / m > 0)
    return 1;
 skip:
  return 0;
}

int main () { return f (1, 0) - 1; }
