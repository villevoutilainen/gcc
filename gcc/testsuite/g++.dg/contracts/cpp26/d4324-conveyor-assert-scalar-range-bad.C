// D4324: a bare contract_assert's own condition is now checked against
// already-established ambient facts before being trusted, the same way
// a callee's own declared precondition is checked at a call site --
// previously this was a real gap: contract_assert only ever established
// facts, never consulted them (see oa_check_assertion_conjunct_against_env
// in gcc/cp/contracts.cc). x is provably 172 here (a real, ordinary
// assignment, tracked the same way any 'if (x < N)' branch already is),
// which flatly contradicts 'x < 30'.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

int
main ()
{
  int x = 42;
  x = 172;
  contract_assert<sc::conveyor_assert_v>(x < 30); // { dg-error "condition .*x < 30.* is provably false" }
                                                   // { dg-message "established \[^\n\]*" "established fact" { target *-*-* } .-1 }
  return 0;
}
