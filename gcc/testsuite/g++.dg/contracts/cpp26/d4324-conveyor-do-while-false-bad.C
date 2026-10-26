// D4324: the counterpart of d4324-conveyor-do-while-false-ok.C -- a
// break or continue taken before a 'do { ... } while (false)' body
// establishes something leaves it unestablished after the block.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>

int g (int &r) conveyor;

int f1 (bool c)
{
  int x = 1;
  int *p = nullptr;
  do
    {
      if (c)
	break;
      p = &x;
    }
  while (false);
  return g (*p); // { dg-error "cannot prove .is_object_address." }
}

int f2 (bool c, int *q)
{
  int x = 1;
  int *p = &x;
  do
    {
      if (c)
	continue;
      p = q;
    }
  while (false);
  return g (*p); // { dg-error "cannot prove .is_object_address." }
}
