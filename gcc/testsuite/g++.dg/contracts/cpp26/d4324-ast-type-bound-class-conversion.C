// D4324: an if condition comparing a class object, through its conversion
// to an integer, with an assignment ('*now <= (t = e->key ())'): the
// type-bound witness took the pointer's type for an integer's bounds and
// crashed (BDE's EventScheduler::chooseNextEvent).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -O1" }

#include <contracts>

struct Atomic { long v; operator long () const { return v; } };
struct Ev { long key () const; };

long choose (Atomic* now, Ev* e)
{
  contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (e));
  long t = 0;
  if (*now <= (t = e->key ()))
    return t;
  return 0;
}
