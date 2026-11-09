// { dg-additional-options { -std=c++26 -fmodules-ts -fcontracts -fcontract-evaluation-semantic=observe } }

import contracts_plain_2;
#include <contracts>

int reports;
void handle_contract_violation (const std::contracts::contract_violation &)
{ ++reports; }

int
main ()
{
  f (1); g (1);
  f (0); g (0);
  if (reports != 2)
    __builtin_abort ();
}
