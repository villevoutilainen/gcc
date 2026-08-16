// D4324: a contract_assert whose condition is merely unprovable (no
// established fact contradicts, or confirms, it) must not be
// spuriously *rejected* -- the "check against ambient facts" step only
// ever hard-errors on a genuine, provable contradiction. Unlike a
// plain -fcontract-conveyor-proofs command-line-wide check, though, an
// unknown conjunct is not silent either (see the discrepancy fix vs.
// a call's own precondition-obligation warning, same session): it
// warns, matching that established convention, but still establishes
// itself as a trusted fact for later code either way.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

int
f (int x) // x is unconstrained here: no fact either confirms or denies x < 30
{
  contract_assert<sc::conveyor_assert_v>(x < 30); // { dg-warning "cannot verify" }
  return x;
}

int
main ()
{
  return f (5);
}
