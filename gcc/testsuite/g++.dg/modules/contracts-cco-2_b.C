// { dg-additional-options { -std=c++26 -fmodules-ts -fcontracts -fcontract-control-objects -fcontracts-client-check=pre } }

import contracts_cco_2;

int
main ()
{
  f (2); a (1); S {}.m (1); t (1);
  if (fails != 0)
    __builtin_abort ();
  f (0);		// 2: pre and post.
  a (0);		// 10.
  S {}.m (0);		// 1.
  t (0);		// 100.
  // And the caller-side checks of the three preconditions: 1 + 1 + 100.
  if (fails != 2 + 10 + 1 + 100 + 102)
    __builtin_abort ();
}
