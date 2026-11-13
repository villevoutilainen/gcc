// D4324: a callee's precondition 'is_object_address (&r.m)' (a member's
// address, not a bare parameter) was never checked at the call by the AST
// engine nor the GIMPLE one: any argument passed.  It is proven with the
// call's arguments.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

using namespace std::contracts;

struct P { int v; };
int read (const P *p) conveyor
  pre<conveyor_assert_v> (std::is_object_address (&p->v))
{
  contract_assert<never_proven_conveyor_v> (std::is_object_address (p));
  return p->v;
}

int ok () conveyor { P x{1}; return read (&x); }
int bad (const P *p) conveyor { return read (p); } // { dg-error "cannot prove 'is_object_address'" }
