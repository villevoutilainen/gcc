// D4324, Increment R: the same pointer-downcast shape stays accepted
// outside a conveyor function -- confirming the restriction is
// conveyor-scoped, not a blanket ban.
//
// Derived's own default constructor is given an explicit, empty body
// -- see d4324-conveyor-static-cast-upcast-ok.C's own identical comment
// for why (an unrelated, separately-discovered pre-existing gap in
// this compiler's defaulted-special-member conveyor-inheritance
// computation, out of scope for this feature; avoided here since it
// has no bearing on what this test actually checks).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct Base { virtual ~Base () {} };
struct Derived : Base { Derived () {} int v; };

int f (Base* b)
{
  Derived* d = static_cast<Derived*> (b);
  return d ? 1 : 0;
}

int main () { Derived d; return f (&d) - 1; }
