// D4324: a contract_assert whose condition is merely unprovable (no
// established fact contradicts, or confirms, it) must not be
// spuriously rejected -- the new "check against ambient facts" step
// only ever diagnoses a genuine, provable contradiction; an unknown
// conjunct falls through to the existing trust-as-fact behavior
// unchanged, exactly as before this fix.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

int
f (int x) // x is unconstrained here: no fact either confirms or denies x < 30
{
  contract_assert<sc::conveyor_assert_v>(x < 30);
  return x;
}

int
main ()
{
  return f (5);
}
