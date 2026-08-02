// D4324/P2680 item 8, Increment L: the same shape again, but with no
// default: label at all -- SWITCH_STMT_ALL_CASES_P is false (an
// unmatched value falls through the switch entirely), so the switch
// must NOT be recognized as terminating, and the merge must stay
// rejected.
// { dg-do compile { target c++26 } }
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
	}
    }
  else
    m = 5;
  return 10 / m; // { dg-error "divisor .m. not provably nonzero in a conveyor function" }
}

int main () { int x = 1; return f (x, true, 0); }
