// D4324: an error in one function doesn't cost another its contracts.
// A contract naming a variable declared 'auto' was dropped once any error
// had been seen, as if its condition were dependent (an unsubstituted
// template's), so the GIMPLE engine lost a trust and reported its use.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;

struct V { int k; int size () const conveyor { return k; } };
V &get (int);

int bad (V *p) { return p->size (); } // { dg-error "cannot prove 'is_object_address' for 'p'" }

int
good (int i)
{
  auto &&r = get (i);
  contract_assert<never_proven_conveyor_v> (std::is_object_address (&r));
  return r.size ();
}

template<typename T>
int
tgood (T i)
{
  auto &&r = get (i);
  contract_assert<never_proven_conveyor_v> (std::is_object_address (&r));
  return r.size ();
}

int use = tgood (1);
