// D4324, Increment Q: an ordinary implicit destructor call, at the end
// of a local variable's scope, is never restricted -- only an
// explicitly-spelled destructor call is.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct S { int v; ~S () {} };

int f () conveyor
{
  S s{1};
  return s.v;
}

int main () { return f () - 1; }
