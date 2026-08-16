// D4324: proven_conveyor on a *precondition*, not just a bare
// contract_assert -- "enables conveyor-proofs in the context it
// appears in": caller() has no -fcontract-conveyor-proofs anywhere in
// dg-additional-options, and no contract of its own, yet still gets a
// hard, strict error for its own unprovable argument to deref's
// precondition, because that precondition's own control object forces
// the caller-side call-obligation-discharge family on regardless of
// the flag (see oa_call_conveyor_obligation_status's own comment,
// gcc/cp/contracts.cc) and strict (proven_conveyor, not analyzed_
// conveyor) makes the unproven case an error there too, not just a
// warning.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int deref (int* p) pre<sc::proven_conveyor_v>(std::is_object_address(p))
{
  return *p;
}

int* global_ptr;

int
caller ()
{
  return deref (global_ptr); // { dg-error "cannot prove .is_object_address. for .global_ptr." }
}

int main () { return caller (); }
