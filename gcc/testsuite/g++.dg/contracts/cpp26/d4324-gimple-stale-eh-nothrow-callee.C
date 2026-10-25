// D4324: a function checked early for its contracts (here its
// contract_assert) is in GIMPLE before a template it calls is
// instantiated; once that callee is found not to throw, the call's
// landing pad is stale, and verifying the function after the GIMPLE
// engine's pass failed ("statement marked for throw, but doesn't").
// Removing it may leave a call on the landing pad (placement new's
// operator delete) unreachable: the call graph must lose its edge.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
#include <new>
using namespace std::contracts;

struct Mutex { int v; void lock () { v = 1; } void unlock () { v = 0; } };

template <class T>
struct Guard
{
  T *m;
  explicit Guard (T *p) : m (p) { if (m) m->lock (); }
  ~Guard () { if (m) m->unlock (); }
};

struct S { Mutex mu; int x; static void *thread (void *arg); };

void *S::thread (void *arg)
{
  S &obj = *static_cast<S *> (arg);
  contract_assert<never_proven_conveyor_v> (std::is_object_address (&obj));
  Guard<Mutex> g (&obj.mu);
  return 0;
}

template <class T>
struct Buf { T v; Buf () : v () {} };

Buf<int> *place (void *mem, int *p)
{
  contract_assert<never_proven_conveyor_v> (std::is_object_address (p));
  return new (mem) Buf<int> ();
}
