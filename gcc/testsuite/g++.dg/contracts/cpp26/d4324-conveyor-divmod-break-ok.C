// D4324/P2680 item 8, Increment L: a bare 'break;' is recognized by
// oa_stmt_terminates_p as never falling through to whatever textually
// follows it -- so the reachability-aware if/else merge (Increment H)
// correctly uses only the else-branch's facts for the merge point
// right after the inner if, still within the loop body.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n, int junk) conveyor
{
  int m = junk;
  for (int i = 0; i < 3; ++i)
    {
      if (n <= 0)
	break;
      else
	m = 5;
      if (10 / m > 0)
	return 1;
    }
  return 0;
}

int main () { return f (1, 0) - 1; }
