// D4324/P2680 item 8, Increment E1: a top-level '&&' conjunct chain in
// a condition is decomposed the same way a contract condition already
// is, applying each conjunct's refinement to the then-branch in
// sequence.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n) conveyor
{
  if (n >= 1 && n <= 100)
    return 10 / n;
  return 0;
}

int main () { return f (5) - 2; }
