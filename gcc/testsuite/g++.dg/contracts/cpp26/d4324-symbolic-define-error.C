// Axiom contracts (~/gcc-axiom-contracts.md): a function declared
// 'symbolic' may never be defined -- it exists purely for contract
// conditions to name, with no runtime representation at all.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

bool is_opened (int* p) symbolic { return p != nullptr; } // { dg-error "a function declared .symbolic. may not be defined" }

int main () { return 0; }
