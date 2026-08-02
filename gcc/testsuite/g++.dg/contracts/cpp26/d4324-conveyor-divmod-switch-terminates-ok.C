// D4324/P2680 item 8, Increment L: a switch that is provably
// exhaustive (a default: label) and provably has no break anywhere
// (SWITCH_STMT_ALL_CASES_P && SWITCH_STMT_NO_BREAK_P, the exact flags
// gcc/c-family/c-common.cc's own c_block_may_fallthru already relies
// on for this question) is recognized by oa_stmt_terminates_p as
// never falling through -- so the merge after the enclosing if
// correctly uses only the else-branch's facts.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n, bool flag, int junk) conveyor
{
  int m = 1;
  if (flag)
    {
      m = junk;
      switch (n)
	{
	case 0:
	  return 1;
	case 1:
	  return 2;
	default:
	  return 3;
	}
    }
  else
    m = 5;
  return 10 / m;
}

int main () { return f (0, false, 0) - 2; }
