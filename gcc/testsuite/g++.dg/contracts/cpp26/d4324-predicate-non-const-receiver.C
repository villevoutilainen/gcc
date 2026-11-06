// D4324: a strict predicate may not call a non-const member function on
// an object it didn't create: c.bump () wrote the caller's object,
// though passing c by non-const reference was already rejected
// (P4445 BUG-15).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

struct C
{
  int v;
  bool bump () conveyor { v = 42; return true; }
  bool ok () const conveyor { return v != 0; }
};

int f (C& c)
  pre<std::contracts::conveyor_assert_v>(c.bump ()) // { dg-error "object 'c' is not owned by the calling function, so the non-const member function 'bool C::bump\\(\\)' may not be called on it" }
{ return c.v; }

int g (C* p)
  pre<std::contracts::conveyor_assert_v>(std::is_object_address (p))
  pre<std::contracts::conveyor_assert_v>(p->bump ()) // { dg-error "non-const member function" }
{ return p->v; }

// A const member function, and the body: fine.
int h (C& c)
  pre<std::contracts::conveyor_assert_v>(c.ok ())
{ return c.bump () ? c.v : 0; }

// An object the predicate creates itself: fine.
int k (int x)
  pre<std::contracts::conveyor_assert_v>(C{x}.bump ())
{ return x; }
