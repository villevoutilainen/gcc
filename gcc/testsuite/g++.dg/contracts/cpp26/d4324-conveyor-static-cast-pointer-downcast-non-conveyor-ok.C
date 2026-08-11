// D4324, Increment R: the same pointer-downcast shape stays accepted
// outside a conveyor function -- confirming the restriction is
// conveyor-scoped, not a blanket ban.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct Base { virtual ~Base () {} };
struct Derived : Base { int v; };

int f (Base* b)
{
  Derived* d = static_cast<Derived*> (b);
  return d ? 1 : 0;
}

int main () { Derived d; return f (&d) - 1; }
