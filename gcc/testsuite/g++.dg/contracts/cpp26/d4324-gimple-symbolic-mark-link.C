// D4324: under the GIMPLE engine a symbolic contract leaves a marker
// (IFN_CONTRACT_MARK) for the analysis to find, which the analysis then
// removes along with its capture struct: no code calls the symbolic
// predicates, which have no definition, so the program links -- at -O0
// too, where nothing else would remove them.
// { dg-do link { target c++26 } }
// { dg-additional-options "-O0 -fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

bool is_opened (int *p) symbolic;

void open_it (int * const p)
  post<std::contracts::never_proven_symbolic_v> (is_opened (p))
{ }

void use (int *p) pre<std::contracts::proven_symbolic_v> (is_opened (p)) { }

int f (int *p)
{
  open_it (p);
  contract_assert<std::contracts::proven_symbolic_v> (is_opened (p));
  use (p);
  return 0;
}

int main ()
{
  int x = 0;
  return f (&x);
}
