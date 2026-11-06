// D4324: a postcondition 'is_object_address (r)' doesn't hold for the
// address of an object the function's return ends -- a by-value parameter
// or an automatic variable -- though it is an object's at the return
// (P4445 BUG-18).  A global, a static, or a pointer passed in is fine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>

namespace sc = std::contracts;

int g;

int *p_param (int i)
  post<sc::conveyor_assert_v>(r: std::is_object_address (r)) // { dg-error "cannot prove 'is_object_address' for 'r'" }
{ return &i; }	// { dg-warning "address of local variable" }

int *p_local ()
  post<sc::conveyor_assert_v>(r: std::is_object_address (r)) // { dg-error "cannot prove 'is_object_address' for 'r'" }
{ int x = 1; return &x; }	// { dg-warning "address of local variable" }

int *p_global ()
  post<sc::conveyor_assert_v>(r: std::is_object_address (r)) { return &g; }

int *p_static ()
  post<sc::conveyor_assert_v>(r: std::is_object_address (r))
{ static int s; return &s; }

int *p_pass (int *q)
  pre<sc::conveyor_assert_v>(std::is_object_address (q))
  post<sc::conveyor_assert_v>(r: std::is_object_address (r)) { return q; }
