// Axiom contracts (~/gcc-axiom-contracts.md): every reachable
// declaration of a symbolic function must repeat 'symbolic' -- a
// strict, symmetric requirement, mirroring D4324's own 'conveyor' rule
// (see d4324-conveyor-redecl-mismatch.C).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

bool is_opened (int* p) symbolic;
bool is_opened (int* p); // { dg-error "redeclaration .* differs in .symbolic. from previous declaration" }

int main () { return 0; }
