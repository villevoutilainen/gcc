// D4324: under observe, side_probe (d4324-side-semantic-shared.h) is
// active on both the client (caller-side wrapper) side and the
// definition side. Both client_calls and definition_calls are checked
// independently (not just their sum) so that a bug firing one side
// twice and the other zero times -- still summing to 2 -- is caught
// rather than passed. See d4324-side-semantic-enforce.C for the
// opposite semantic, where only the client side is active.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontracts-client-check=pre -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "d4324-side-semantic-shared.h"

int main ()
{
  if (f (1) != 1)
    __builtin_abort ();
  if (client_calls != 1)
    __builtin_abort ();
  if (definition_calls != 1)
    __builtin_abort ();
  return 0;
}
