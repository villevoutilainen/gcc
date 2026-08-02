// D4324: 'conveyor' is context-sensitive (like 'override'/'final'), not a
// reserved word -- even WITH -fcontract-control-objects enabled, using
// 'conveyor' as an ordinary identifier anywhere outside the one trailing
// function-specifier position continues to parse exactly as before.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int conveyor = 5;
int f (int conveyor) { return conveyor; }

int main () { return f (conveyor) - 5; }
