// D4324: a never-proven predicate is checked against the facts at its
// check, the facts about the members of the objects it is passed
// included: a trusted pointer or reference member ('g.p', 'this->r')
// is an object in a later predicate that uses it, as in a conveyor
// predicate.  Without that trust it still isn't.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;

struct V { int n; int size () const conveyor { return n; } };
struct G { const V *p; const V &r; };

int f (const G &g) pre<conveyor_assert_v> (std::is_object_address (&g))
{
  contract_assert<never_proven_conveyor_v> (std::is_object_address (g.p));
  contract_assert<never_proven_conveyor_v> (g.p->size () > 0);
  return 0;
}

int h (const G *g) pre<conveyor_assert_v> (std::is_object_address (g))
{
  contract_assert<never_proven_conveyor_v> (std::is_object_address (&g->r));
  contract_assert<never_proven_conveyor_v> (g->r.size () > 0);
  return 0;
}

struct H
{
  const V &r;
  int k () const
  {
    contract_assert<never_proven_conveyor_v> (std::is_object_address (this));
    contract_assert<never_proven_conveyor_v> (std::is_object_address (&r));
    contract_assert<never_proven_conveyor_v> (r.size () > 0);
    return 0;
  }
};

int u (const G &g) pre<conveyor_assert_v> (std::is_object_address (&g))
{
  contract_assert<never_proven_conveyor_v> (g.r.size () > 0); // { dg-error "cannot prove 'is_object_address' for 'g.G::r'" }
  return 0;
}
