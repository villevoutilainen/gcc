// D4324: never_proven exempts a contract_assert from the *static*
// "check against ambient facts" step entirely, unconditionally -- even
// a conjunct that is genuinely, provably false produces no compile-
// time diagnostic at all (not even a warning) when the control object
// is never_proven, regardless of -fcontract-conveyor-proofs being on.
// Confirmed via direct experiment: the same shape without never_
// proven_conveyor_v (see d4324-conveyor-assert-unknown-ok.C's own
// sibling) does produce a diagnostic -- this is a real exemption, not
// something that happens to be silent anyway.
//
// never_proven only ever suppresses the *static* pass -- it has no
// effect on the ordinary *runtime* evaluation semantic (enforce, here,
// by default), which still genuinely evaluates 'x < 30' and correctly
// reports a violation, since x really is 172 at that point. This test
// is deliberately compile-only, not dg-do run, for exactly that
// reason -- see d4324-never-proven-symbolic-ok.C for a variant that
// safely runs, since a symbolic-tagged contract has no runtime
// representation at all by default.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

int
f (int x)
{
  x = 172;
  contract_assert<sc::never_proven_conveyor_v>(x < 30);
  return x;
}

int main () { return f (5) - 172; }
