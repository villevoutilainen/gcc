// D4324/P2680 item 8, Increment E1: a preceding 'if (n > 0)' guard
// establishes a provable value range for n (excluding zero) in the
// then-branch, sufficient for the div/mod restriction to accept a
// division by n there -- the paper-adjacent "full symbolic range
// analysis" case explicitly requested over the narrower constant-only
// alternative.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n) conveyor
{
  if (n > 0)
    return 10 / n;
  return 0;
}

int main () { return f (5) - 2; }
