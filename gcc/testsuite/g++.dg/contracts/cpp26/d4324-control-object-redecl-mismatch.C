// A declared contract's own control object (pre<A>/pre<B>) must match
// across every declaration of "the same" contract, exactly like the
// condition itself already must -- otherwise which control object
// actually governs a precondition/postcondition/contract_assert would
// silently depend on which declaration a given caller (or the
// definition itself) happens to see. Found via direct testing: this was
// previously accepted with no diagnostic at all, since mismatched_
// contracts_p only ever compared CONTRACT_CONDITION, never CONTRACT_
// CONTROL_OBJECT.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

void f (int x) pre<std::contracts::review_v>(x >= 0);
void f (int x) pre<std::contracts::default_v>(x >= 0) { } // { dg-error "mismatched contract control object in declaration" }

int main () { f (1); }
