// D4324: an object of static storage duration may be ended by any call
// that isn't conveyor, given a route to it or not -- any function can
// name it (the lifetime rule) -- but not by a builtin; a trust through a
// reference makes it an object again.  A static local's declaration
// doesn't make it one again on a later pass of a loop.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs -fcontract-analysis-engine=ast" }

#include <contracts>
#define ALIVE(x) contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (&x))

struct H { int v; void cm () const conveyor {} };
void other ();
void touch (H&);
H g;
static H sg;
constexpr H cg{};

void no_route () { g.cm (); other (); g.cm (); } // { dg-error "cannot prove .is_object_address." }
void routed () { sg.cm (); touch (sg); sg.cm (); } // { dg-error "cannot prove .is_object_address." }
void constant () { cg.cm (); other (); cg.cm (); } // { dg-error "cannot prove .is_object_address." }
void trusted () { other (); H& r = g; ALIVE (r); g.cm (); }
void builtin () { __builtin_free (nullptr); g.cm (); }
void local () { H l; other (); l.cm (); }
void static_local ()
{
  for (int i = 0; i < 2; ++i)
    {
      static H s;
      s.cm (); // { dg-error "cannot prove .is_object_address." }
      touch (s);
    }
}
