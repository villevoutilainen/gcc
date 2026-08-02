// D4324/P2680 item 8, Increment L: a 'while (true)' loop with no
// break belonging to it is recognized by oa_stmt_terminates_p as
// never falling through -- confirmed via debug_tree that the
// condition is exactly a bare nonzero INTEGER_CST at this pass's
// timing. oa_loop_has_own_break_p (a cp_walk_tree scan, pruning
// descent at any nested loop/switch) confirms no break belongs to
// this specific loop.
// { dg-do run { target c++26 } }
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
	    return 10 / n;
	  n++;
	}
    }
  else
    m = 5;
  return 10 / m;
}

int main () { return f (0, false, 0) - 2; }
