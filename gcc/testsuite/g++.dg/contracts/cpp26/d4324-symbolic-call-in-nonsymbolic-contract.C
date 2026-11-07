// D4324: a function declared 'symbolic' has no definition, so a contract
// that is neither symbolic- nor conveyor-checked, which would evaluate it
// at run time, may not call it: a compile-time error rather than a link
// failure (P4445 BUG-28).  A strict one may: there the call is true, or
// the recorded fact (d4324-symbolic-call-in-strict-run.C).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

bool positive (int) symbolic; // { dg-message "declared here" }

void a (int x) pre (positive (x)) {} // { dg-error "declared 'symbolic', may only be used in the condition of a symbolic- or conveyor-checked contract" }
void b (int x) pre<sc::conveyor_assert_v>(positive (x)) {}
void c (int x) pre<sc::symbolic_assert_v>(positive (x)) {}
