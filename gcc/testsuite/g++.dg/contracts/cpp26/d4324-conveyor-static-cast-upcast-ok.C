// D4324, Increment R: an ordinary derived-to-base (upcast) static_cast
// is never restricted, through either a pointer or a reference --
// matches the paper's "downcast only" scope.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct Base { virtual ~Base () {} };
struct Derived : Base { int v; };

int f (Derived* d) conveyor
{
  Base* b = static_cast<Base*> (d);
  return b ? 1 : 0;
}

int main () { Derived d; return f (&d) - 1; }
