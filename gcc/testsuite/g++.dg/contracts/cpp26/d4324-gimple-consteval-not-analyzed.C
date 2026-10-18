// D4324: the GIMPLE engine doesn't analyze an immediate function.  Its
// body is never genericized or emitted, so gimplifying it ICEd (seen
// on mdspan's __padded_block_begin_generic).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }

#include <contracts>

consteval int count (int n) conveyor
{
  int i = 0;
  while (i < n && i < 10)
    ++i;
  if (i > 5)
    return i;
  return 0;
}

int main () { return count (3); }
