// D4324: std::is_object_address is resolved at compile time and replaced
// by its verdict, which is only sound for a conjunct of the condition:
// under '||', '!' or '?:' it would be evaluated as a constant, so it's
// ill-formed there (P4445 BUG-16).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

int a (int *p, int x)
  pre<sc::conveyor_assert_v>(std::is_object_address (p) || x > 0) // { dg-error "may only be used as a conjunct" }
{ return x; }
int b (int *p)
  pre<sc::conveyor_assert_v>(!std::is_object_address (p)) // { dg-error "may only be used as a conjunct" }
{ return 0; }
int c (int *p, int x)
  pre<sc::conveyor_assert_v>(x > 0 && std::is_object_address (p)) { return *p; }
int d (int *p, bool k)
  pre<sc::conveyor_assert_v>(k ? std::is_object_address (p) : true) // { dg-error "may only be used as a conjunct" }
{ return 0; }
int e (int *p)
  pre<sc::conveyor_assert_v>((std::is_object_address (p))) { return *p; }
