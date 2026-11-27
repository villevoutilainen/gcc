// -fcontract-analysis-proofs=plugin leaves the proof checks to a contract
// analysis plugin; without one loaded, nothing would make them.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-proofs=plugin" }
// { dg-error "needs a contract analysis plugin, but none is loaded" "" { target *-*-* } 0 }

int f (int x);
