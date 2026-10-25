// D4324: a call whose class-type result initializes the return value or
// a local directly -- an AGGR_INIT_EXPR for an ordinary function, not a
// constructor -- has its precondition obligations checked like any
// other call.  The AST engine took its first argument for a
// constructor's receiver placeholder and skipped it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct A { int v; A (); A (const A &); };
A make (const int &r) pre<std::contracts::conveyor_assert_v> (std::is_object_address (&r));
int take (const int &r) pre<std::contracts::conveyor_assert_v> (std::is_object_address (&r));
struct S
{
  A get (const int &r) const pre<std::contracts::conveyor_assert_v> (std::is_object_address (&r));
};

A returned (const int &a) { return make (a); } // { dg-error "cannot prove .is_object_address." }
int scalar (const int &a) { return take (a); } // { dg-error "cannot prove .is_object_address." }
A local (const int &a) { A x = make (a); return x; } // { dg-error "cannot prove .is_object_address." }
A member (S s, const int &a) { return s.get (a); } // { dg-error "cannot prove .is_object_address." }

A proven (const int &a)
  pre<std::contracts::never_proven_conveyor_v> (std::is_object_address (&a))
{ return make (a); }
