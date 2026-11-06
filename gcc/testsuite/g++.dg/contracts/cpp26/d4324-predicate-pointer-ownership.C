// D4324: a strict predicate owns no object it didn't create, so it may pass
// a pointer to non-const, which the callee may write through, only to an
// object it created; a pointer to const may point anywhere (a conveyor
// function writes through const access only to an object that began its
// lifetime in it).  As for a non-const reference or a non-const member
// call (P4445: a strict precondition modified the caller's object through
// a pointer parameter).  A conveyor function's own body may pass on what
// it received.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

bool touch (int *p) conveyor
  pre<sc::conveyor_assert_v>(std::is_object_address (p))
{ *p = 42; return true; }

bool look (const int *p) conveyor
  pre<sc::conveyor_assert_v>(std::is_object_address (p))
{ return *p > 0; }

void f (int *p)
  pre<sc::conveyor_assert_v>(std::is_object_address (p) && touch (p));	// { dg-error "object '\\* p' is not owned by the calling function, so it may not be passed as the pointer-to-non-const parameter" }

void g (int x)
  pre<sc::conveyor_assert_v>(touch (&x));	// { dg-error "object 'x' is not owned" }

void h (int *p)
  pre<sc::conveyor_assert_v>(std::is_object_address (p) && look (p));

void k (int x)
  pre<sc::conveyor_assert_v>(look (&x));

bool forward (int *p) conveyor
  pre<sc::conveyor_assert_v>(std::is_object_address (p))
{ return touch (p); }
