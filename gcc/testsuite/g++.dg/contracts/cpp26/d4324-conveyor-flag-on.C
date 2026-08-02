// D4324: 'conveyor' is a context-sensitive trailing function-specifier,
// recognized under -fcontract-control-objects, with no ABI/mangling impact.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor { return x + 1; }

int main () { return f (1) - 2; }
