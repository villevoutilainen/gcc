// D4324: contract_assert<proven_symbolic_v> verifying an int is in
// range -- PROVABLY FALSE. 'i' is pinned to a concrete, out-of-range
// value: 'i >= 0' is true (silently accepted, no diagnostic of its
// own) but 'i < 10' is a genuine, provable contradiction -- reported
// with the sharper "is provably false" wording, not "cannot prove"
// (contrast d4324-proven-symbolic-assert-range-unknown-bad.C, where
// neither conjunct can be resolved either way). Confirms each
// conjunct is proven or rejected independently, not as a single,
// all-or-nothing unit.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

int
demo_false ()
{
  int i = 20;
  contract_assert<sc::proven_symbolic_v>(i >= 0
					  && i < 10); // { dg-error "is provably false" }
  return 0;
}

int main () { return demo_false (); }
