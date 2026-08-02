// D4324/P2680 item 8, Increment L: the same shape, but the loop
// contains a 'break;' -- oa_loop_has_own_break_p must find it, so the
// loop is NOT recognized as terminating, and the merge must stay
// rejected.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n, bool flag, int junk) conveyor
{
  int m = 1;
  if (flag)
    {
      m = junk;
      while (true)
	{
	  if (n > 0)
	    break;
	  n++;
	}
    }
  else
    m = 5;
  return 10 / m; // { dg-error "divisor .m. not provably nonzero in a conveyor function" }
}

int main () { int x = 1; return f (x, true, 0); }
