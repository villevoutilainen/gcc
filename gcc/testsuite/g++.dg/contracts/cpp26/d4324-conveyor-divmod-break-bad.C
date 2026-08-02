// D4324/P2680 item 8, Increment L: sanity check -- without the break
// (falling through normally instead), the merge must still AND both
// branches' facts and correctly reject, confirming the break-specific
// recognition is genuinely order/shape-sensitive, not a blanket
// relaxation.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n, int junk) conveyor
{
  int m = junk;
  for (int i = 0; i < 3; ++i)
    {
      if (n <= 0)
	;
      else
	m = 5;
      if (10 / m > 0) // { dg-error "divisor .m. not provably nonzero in a conveyor function" }
	return 1;
    }
  return 0;
}

int main () { int x = 1; return f (x, 0); }
