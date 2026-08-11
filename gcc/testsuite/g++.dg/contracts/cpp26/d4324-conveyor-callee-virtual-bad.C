// D4324: a genuinely virtual call is banned outright from conveyor-
// restricted code, even when the statically-named override is itself
// declared 'conveyor' -- a temporary restriction (per the user's own
// explicit decision), since the compiler has no way yet to know every
// possible override in the hierarchy is also conveyor ("conveyor-ness"
// is not yet a checked, inherited property of an override).
//
// Base/Derived's own destructors are deliberately left non-conveyor:
// declaring a virtual destructor conveyor drags in its own compiler-
// generated "deleting destructor" clone's call to operator delete
// (irrelevant to what this test checks) -- a separate, disclosed
// follow-on gap, not fixed here.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct Base
{
  virtual int f (int x) conveyor { return x; }
  virtual ~Base () {}
};

struct Derived : Base
{
  int f (int x) conveyor override { return x + 1; }
};

int g (Base *b) conveyor
{
  b->f (1); // { dg-error "virtual function call not permitted" }
  return 0;
}

int main () { Derived d; return g (&d); }
