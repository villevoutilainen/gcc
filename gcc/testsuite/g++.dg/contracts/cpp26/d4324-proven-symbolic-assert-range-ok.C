// D4324: contract_assert<proven_symbolic_v> verifying an int is in
// range -- proof TRUE. An earlier if-condition establishes 'i >= 0 &&
// i < 10' as a fact; the contract_assert immediately below restating
// the identical range is then genuinely proven, not just assumed --
// with no -fcontract-symbolic-proofs anywhere in dg-additional-
// options, since proven_symbolic forces this analysis on by itself.
// See d4324-proven-symbolic-assert-range-unknown-bad.C/
// d4324-proven-symbolic-assert-range-false-bad.C for the two ways
// this same shape is rejected instead.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int
demo_true (int i)
{
  if (i >= 0 && i < 10)
    contract_assert<sc::proven_symbolic_v>(i >= 0 && i < 10);
  return 0;
}

int main () { return demo_true (5); }
