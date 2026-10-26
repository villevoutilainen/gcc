// D4324: 'do { ... } while (false)' -- the shape of every assertion macro
// -- is walked once as a block rather than as a loop: what its body
// establishes holds after it, and a break or continue leaves it with
// what holds where it's taken.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>

int g (int &r) conveyor;

int f1 ()
{
  int x = 1;
  int *p = nullptr;
  do { p = &x; } while (false);
  return g (*p);
}

int f2 (bool c)
{
  int x = 1, y = 2;
  int *p = &x;
  do
    {
      if (c)
	break;
      p = &y;
    }
  while (0);
  return g (*p);
}

int f3 (bool c)
{
  int x = 1, y = 2;
  int *p = &x;
  do
    {
      if (c)
	continue;
      p = &y;
    }
  while (false);
  return g (*p);
}

int f4 ()
{
  int x = 1;
  int *p = nullptr;
  do
    do { p = &x; } while (false);
  while (false);
  return g (*p);
}
