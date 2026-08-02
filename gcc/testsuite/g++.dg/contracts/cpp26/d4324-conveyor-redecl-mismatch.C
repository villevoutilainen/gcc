// D4324: every reachable declaration of a conveyor function must repeat
// 'conveyor' -- a strict, symmetric requirement (unlike contracts, which
// may be omitted on a redeclaration).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor;
int f (int x) { return x; } // { dg-error "redeclaration .* differs in .conveyor. from previous declaration" }

int main () { return f (1); }
