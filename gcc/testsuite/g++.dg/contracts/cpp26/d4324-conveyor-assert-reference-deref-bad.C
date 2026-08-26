// D4324/P2680: a bare reference read inside a conveyor contract_assert
// must require the same is_object_address proof a raw pointer
// dereference or a 'this'-via-member-access already required -- "a
// conveyor contract_assert's predicate requires object validity when
// the predicate... uses a reference, dereferences a pointer, or uses a
// 'this' pointer by accessing a member" (Ville). Before this fix, the
// mandatory pointer-dereference-validity check (oa_scan_array_bounds_
// in_expr's own INDIRECT_REF/"Increment W2" fallback) explicitly
// excluded every REFERENCE_TYPE operand, on the theory that a bound
// reference is always valid by construction -- true for a local
// reference/capture binding, but not for a reference *parameter*,
// whose binding comes from an external, unverified caller. This is a
// local, definition-side-only diagnostic (matching the existing
// pointer/'this' checks' own severity), never a new caller-facing
// obligation -- there is no requirement to turn a body assertion into
// a precondition.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>

void f (int& x)
{
  contract_assert<std::contracts::conveyor_assert_v> (x < 2048); // { dg-error "pointer dereference of .x. not provably valid" }
						 // { dg-warning "cannot verify .contract_assert. condition" "" { target *-*-* } .-1 }
}

int main () { return 0; }
