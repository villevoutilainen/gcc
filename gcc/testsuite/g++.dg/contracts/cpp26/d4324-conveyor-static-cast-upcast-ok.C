// D4324, Increment R: an ordinary derived-to-base (upcast) static_cast
// is never restricted, through either a pointer or a reference --
// matches the paper's "downcast only" scope.
//
// Derived's own default constructor is given an explicit, empty body
// (rather than left implicitly-defaulted) to sidestep an unrelated,
// separately-discovered pre-existing gap in this compiler's defaulted-
// special-member conveyor-inheritance computation (method.cc's own
// "a defaulted special member is conveyor only if every corresponding
// base/member special member is conveyor too" rule): a defaulted
// constructor whose base class has a non-trivial destructor needs an
// implicit EH cleanup calling that destructor, and this compiler was
// found to sometimes mis-derive such a defaulted special member's own
// conveyor-ness as true even though the corresponding base member
// (here, Base's own implicit, trivial default constructor) isn't --
// out of scope for this feature to fix here; giving Derived a real,
// user-provided default constructor avoids the defaulted-special-
// member path entirely and has no bearing on what this test actually
// checks (static_cast permissions).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct Base { virtual ~Base () {} };
struct Derived : Base { Derived () {} int v; };

int f (Derived* d) conveyor
{
  Base* b = static_cast<Base*> (d);
  return b ? 1 : 0;
}

int main () { Derived d; return f (&d) - 1; }
