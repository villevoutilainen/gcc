// D4324/P2680 item 8, Increment L: a bare 'continue;' is recognized
// by oa_stmt_terminates_p the same way 'break;' is -- it never falls
// through to whatever textually follows it in the loop body, so the
// merge right after the inner if correctly uses only the else-
// branch's facts.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n, int junk) conveyor
{
  int m = junk;
  for (int i = 0; i < 3; ++i)
    {
      if (n <= 0)
	continue;
      else
	m = 5;
      if (10 / m > 0)
	return 1;
    }
  return 0;
}

int main () { return f (1, 0) - 1; }
