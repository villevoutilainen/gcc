// D4324: without -fcontract-control-objects, 'conveyor' is not recognized as
// a trailing function-specifier at all -- it remains an ordinary,
// unreserved identifier, usable as a variable/parameter/function name
// exactly as before this feature existed.
// { dg-do run { target c++26 } }

int conveyor = 5;
int f (int conveyor) { return conveyor; }

int main () { return f (conveyor) - 5; }
