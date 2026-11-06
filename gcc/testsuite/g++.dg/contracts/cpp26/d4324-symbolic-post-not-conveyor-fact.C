// D4324: a symbolic postcondition, which is never evaluated, can't discharge
// a conveyor precondition; a conveyor one can, and a symbolic one still
// discharges a symbolic precondition (P4445 BUG-20).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs -fcontract-analysis-engine=ast" }

#include <contracts>

namespace sc = std::contracts;

int divide (int a, int b) conveyor pre<sc::proven_conveyor_v>(b != 0);
int sdivide (int a, int b) pre<sc::proven_symbolic_v>(b != 0);
int hash_symb (int x) post<sc::symbolic_assert_v>(r: r != 0);
int hash_conv (int x) post<sc::conveyor_assert_v>(r: r != 0);
int hash_range (int x) post<sc::symbolic_assert_v>(r: r > 0);

int d1 (int x) { return divide (10, hash_symb (x)); } // { dg-error "cannot prove" }
int d2 (int x) { return divide (10, hash_conv (x)); }
int d3 (int x) { return sdivide (10, hash_symb (x)); }
int d4 (int x) { return divide (10, hash_range (x)); } // { dg-error "cannot prove" }
