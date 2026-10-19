// D4324/P2680 item 8: a 'return;' in a void conveyor function has no
// value for the overflow and division scans to look at (it used to
// crash them).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int g;

void f (int i) conveyor
{
  if (i < 0)
    return;
  for (; i < 9; i++)
    ;
}
