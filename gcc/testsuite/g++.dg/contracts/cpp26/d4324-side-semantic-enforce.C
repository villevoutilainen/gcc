// D4324: under enforce, side_probe (d4324-side-semantic-shared.h) is
// active only on the client (caller-side wrapper) side; the
// definition-side copy of the same contract is ignored and does
// nothing. See d4324-side-semantic-observe.C for the opposite semantic,
// where both sides are active.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontracts-client-check=pre -fcontract-evaluation-semantic=enforce" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "d4324-side-semantic-shared.h"

int main ()
{
  if (f (1) != 1)
    __builtin_abort ();
  if (client_calls != 1)
    __builtin_abort ();
  if (definition_calls != 0)
    __builtin_abort ();
  return 0;
}
