// D4324: a function declared 'symbolic' names a fact and has no
// definition: only a symbolic contract's condition, which is never
// evaluated, may name it -- not an ordinary contract, which would call it
// (a compile-time error rather than a link failure, P4445 BUG-28), nor a
// strict one, which is evaluated too.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

bool positive (int) symbolic; // { dg-message "declared here" }
void a (int x) pre (positive (x)) {} // { dg-error "declared 'symbolic', may only be used in the condition of a symbolic contract" }
void b (int x) pre<sc::conveyor_assert_v>(positive (x)) {} // { dg-error "declared 'symbolic', may only be used in the condition of a symbolic contract" }
void c (int x) pre<sc::symbolic_assert_v>(positive (x)) {}
