// D4324: GIMPLE-engine counterpart of
// d4324-conveyor-class-result-call-obligation.C.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

struct A { int v; A (); A (const A &); };
A make (const int &r) pre<std::contracts::never_proven_conveyor_v> (std::is_object_address (&r));
int take (const int &r) pre<std::contracts::never_proven_conveyor_v> (std::is_object_address (&r));
struct S
{
  A get (const int &r) const pre<std::contracts::never_proven_conveyor_v> (std::is_object_address (&r));
};

A returned (const int &a) { return make (a); } // { dg-error "cannot prove .is_object_address." }
int scalar (const int &a) { return take (a); } // { dg-error "cannot prove .is_object_address." }
A local (const int &a) { A x = make (a); return x; } // { dg-error "cannot prove .is_object_address." }
A member (S s, const int &a) { return s.get (a); } // { dg-error "cannot prove .is_object_address." }

A proven (const int &a)
  pre<std::contracts::never_proven_conveyor_v> (std::is_object_address (&a))
{ return make (a); }
