// D4324: repeating 'conveyor' consistently across every declaration is
// well-formed.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor;
int f (int x) conveyor { return x; }

int main () { return f (1) - 1; }
