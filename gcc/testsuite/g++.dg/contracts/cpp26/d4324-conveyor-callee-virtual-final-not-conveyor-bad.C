// D4324: the same devirtualized shape as d4324-conveyor-callee-virtual-
// final-ok.C, but the resolved override is NOT declared 'conveyor' --
// devirtualization only exempts the call from the *virtual*-call ban,
// it does not exempt it from the ordinary callee-must-be-conveyor rule.
//
// See d4324-conveyor-callee-virtual-bad.C's own comment for why the
// destructors here are deliberately left non-conveyor and the classes
// are given explicit default constructors.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct Base
{
  Base () {}
  virtual int f (int x) conveyor { return x; }
  virtual ~Base () {}
};

struct Derived final : Base
{
  Derived () {}
  int f (int x) override { return x + 1; }
};

int g (Derived *d) conveyor
{
  d->f (1); // { dg-error "not declared .conveyor." }
  return 0;
}

int main () { Derived d; return g (&d); }
