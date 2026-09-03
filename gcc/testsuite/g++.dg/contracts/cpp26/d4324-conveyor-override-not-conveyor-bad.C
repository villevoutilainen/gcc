// D4324: conveyor-ness is never automatically inherited by an override
// (unlike an ordinary contract, see maybe_inherit_virtual_contract) --
// an override of a 'conveyor' virtual function that is not itself
// declared 'conveyor' is ill-formed, diagnosed right at the override
// declaration, regardless of whether anything ever actually calls it
// virtually. This is what makes it sound to permit an ordinary virtual
// call to a 'conveyor' virtual function from conveyor-restricted code
// (see d4324-conveyor-callee-virtual-ok.C): every possible override
// reachable through that vtable slot is guaranteed conveyor too.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct Base
{
  virtual int f (int x) conveyor { return x; }
  virtual ~Base () {}
};

struct Derived : Base
{
  int f (int x) override { return x; } // { dg-error "must itself be declared .conveyor." }
};

int main () { Derived d; return d.f (1) - 1; }
