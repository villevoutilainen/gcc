// D4324/P2680 item 8: closes the "assignment-in-condition" gap --
// 'if ((i = compute (q)) > 0)' now updates i's tracked range fact for
// the then-branch (via oa_refine_single_comparison recognizing an
// assignment as the compared operand's value), sufficient for the
// div/mod restriction to accept a division by i there.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int compute (int);

int f (int q) conveyor
{
  int i = 0;
  if ((i = compute (q)) > 0)
    return 10 / i;
  return 0;
}

int compute (int q) { return q; }

int main () { return f (5) - 2; }
