// { dg-additional-options { -std=c++26 -fmodules-ts -fcontracts -fcontract-control-objects } }

import contracts_cco_gmf;

int
main ()
{
  Q q (1);
  make_r (1);
  if (gmf_fails != 0)
    __builtin_abort ();
  Q q0 (0);
  make_r (0);
  R<long> r0 (0);
  if (gmf_fails != 3)
    __builtin_abort ();
}
