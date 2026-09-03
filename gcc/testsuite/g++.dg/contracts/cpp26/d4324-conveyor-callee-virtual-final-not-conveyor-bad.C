// D4324: the same devirtualized shape as d4324-conveyor-callee-virtual-
// final-ok.C, but the resolved override is NOT declared 'conveyor'.
// Devirtualization is irrelevant to whether this compiles: conveyor-
// ness is never automatically inherited by an override (see
// maybe_inherit_virtual_contract, which does this for ordinary
// contracts but deliberately not for conveyor-ness), so Derived::f is
// ill-formed as an override of a 'conveyor' virtual function the
// moment it's declared, regardless of 'final' and regardless of
// whether the call below is ever reached (see
// d4324-conveyor-override-not-conveyor-bad.C for the same rejection
// without 'final' in the picture at all).
//
// See d4324-conveyor-callee-virtual-ok.C's own comment for why the
// destructors here are deliberately left non-conveyor.
// See .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct Base
{
  virtual int f (int x) conveyor { return x; }
  virtual ~Base () {}
};

struct Derived final : Base
{
  int f (int x) override { return x + 1; } // { dg-error "must itself be declared .conveyor." }
};

int g (Derived *d) conveyor
{
  // Derived::f is still, despite its own error above, an ordinary
  // (non-conveyor) member function after error recovery, so this call
  // also independently trips the ordinary callee-must-be-conveyor rule.
  d->f (1); // { dg-error "not declared .conveyor." }
  return 0;
}

int main () { Derived d; return g (&d); }
