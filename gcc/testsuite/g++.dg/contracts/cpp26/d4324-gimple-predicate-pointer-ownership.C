// D4324: d4324-predicate-pointer-ownership.C under the GIMPLE engine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

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
