// D4324/P2680: the GIMPLE-flavor mirror of d4324-conveyor-assert-
// reference-deref-bad.C -- cg_check_one_dereference_candidate
// (contracts-gimple.cc) needed the identical fix as its AST-side
// counterpart (oa_scan_array_bounds_in_expr, contracts.cc): a
// reference-typed *parameter* now requires the same is_object_address
// proof a raw pointer dereference already required, while any other
// REFERENCE_TYPE operand (a local reference/capture) remains exempt.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }

#include <contracts>

void f (int& x)
{
  contract_assert<std::contracts::conveyor_assert_v> (x < 2048); // { dg-error "pointer dereference of .x. not provably valid" }
}

int main () { return 0; }
