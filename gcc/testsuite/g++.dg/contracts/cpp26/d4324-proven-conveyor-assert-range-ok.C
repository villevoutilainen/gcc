// D4324: contract_assert<proven_conveyor_v> verifying an int is in
// range -- proof TRUE. Conveyor mirror of
// d4324-proven-symbolic-assert-range-ok.C, in every respect: an
// earlier if-condition establishes 'i >= 0 && i < 10' as a fact; the
// contract_assert immediately below restating the identical range is
// then genuinely proven, not just assumed -- with no -fcontract-
// conveyor-proofs anywhere in dg-additional-options, since proven_
// conveyor forces this analysis on by itself. See
// d4324-proven-conveyor-assert-range-unknown-bad.C/
// d4324-proven-conveyor-assert-range-false-bad.C for the two ways
// this same shape is rejected instead.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int
demo_true (int i)
{
  if (i >= 0 && i < 10)
    contract_assert<sc::proven_conveyor_v>(i >= 0 && i < 10);
  return 0;
}

int main () { return demo_true (5); }
