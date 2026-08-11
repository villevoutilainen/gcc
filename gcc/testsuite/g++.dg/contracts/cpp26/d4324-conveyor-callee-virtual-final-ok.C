// D4324: a call through a 'final'-marked override is statically
// devirtualized (LOOKUP_NONVIRTUAL gets set before the callee-must-be-
// conveyor check runs), so it's treated as an ordinary call, not banned
// as virtual -- and, since the resolved target is itself declared
// 'conveyor', the call is legal.
//
// See d4324-conveyor-callee-virtual-bad.C's own comment for why the
// destructors here are deliberately left non-conveyor and the classes
// are given explicit default constructors.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
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
  int f (int x) conveyor override { return x + 1; }
};

int g (Derived *d) conveyor
{
  return d->f (1);
}

int main () { Derived d; return g (&d) - 2; }
