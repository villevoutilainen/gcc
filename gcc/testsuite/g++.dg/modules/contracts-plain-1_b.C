// { dg-additional-options { -std=c++26 -fmodules-ts -fcontracts -fcontract-evaluation-semantic=observe } }

import contracts_plain;
#include <contracts>

int reports;
void handle_contract_violation (const std::contracts::contract_violation &)
{ ++reports; }

int own (int x) pre (x > 0) { return x; }

int
main ()
{
  f (1); g (2); own (1);
  if (reports != 0)
    __builtin_abort ();
  f (0);		// 1: the contract_assert.
  g (0);		// 2: pre and post.
  own (0);		// 1: the importer's own.
  if (reports != 4)
    __builtin_abort ();
}
