// D4324: a function declared 'symbolic' has no definition, so a contract
// that isn't symbolic-checked, which would evaluate it at run time, may not
// call it: a compile-time error rather than a link failure (P4445 BUG-28).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

bool positive (int) symbolic; // { dg-message "declared here" }

void a (int x) pre (positive (x)) {} // { dg-error "declared 'symbolic', may only be used in the condition of a symbolic-checked contract" }
// A conveyor predicate may only call conveyor functions anyway.
void b (int x) pre<sc::conveyor_assert_v>(positive (x)) {} // { dg-error "not declared 'conveyor'" }
void c (int x) pre<sc::symbolic_assert_v>(positive (x)) {}
