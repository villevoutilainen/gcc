// D4324/P2680 item 8, Increment E1: the else-branch of a single,
// non-compound condition is also refined -- 'n <= 0' being false in
// the else branch implies 'n > 0', excluding zero.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n) conveyor
{
  if (n <= 0)
    return 1;
  else
    return 10 / n;
}

int main () { return f (5) - 2; }
