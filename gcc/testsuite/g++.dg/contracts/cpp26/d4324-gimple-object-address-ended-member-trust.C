// D4324: a call given a route to a member (a reference a member call on
// it returned) ends the standard-layout object the member lies in; a
// trust of another member then makes that member an object again, for
// the address a later use computes anew.  Without the trust, it isn't.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
#define ALIVE(x) contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (&x))

struct It { const char* p; It& operator++ () conveyor { ++p; return *this; } };
struct St { int op; int opcode () const; };
struct V
{
  const St& at (unsigned long) const conveyor
    post<std::contracts::never_proven_conveyor_v> (r: std::is_object_address (&r));
};
struct E
{
  V nfa;
  It cur;
  int trusted (int n);
  int untrusted (int n);
  int retrusted_then_ended (int n);
};
void touch (It&);

int E::trusted (int n)
{
  contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (this));
  const St& s = nfa.at (n);
  if (s.opcode () != 1)
    return n;
  ALIVE (cur);
  ++cur;
  ++cur;
  return 0;
}

int E::untrusted (int n)
{
  contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (this));
  const St& s = nfa.at (n);
  if (s.opcode () != 1)
    return n;
  ++cur; // { dg-error "cannot prove .is_object_address." }
  return 0;
}

int E::retrusted_then_ended (int n)
{
  contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (this));
  const St& s = nfa.at (n);
  s.opcode ();
  ALIVE (cur);
  touch (cur);
  ++cur; // { dg-error "cannot prove .is_object_address." }
  return 0;
}

