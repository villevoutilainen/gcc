// D4324: a conveyor function calling one that isn't conveyor is told where
// the callee is declared -- that's where 'conveyor' goes.
// { dg-do compile { target c++26 } }
// { dg-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int g () { return 1; } // { dg-message "'int g\\(\\)' declared here" }

int
f () conveyor
{
  return g (); // { dg-error "call to 'int g\\(\\)', which is not declared 'conveyor'" }
}

struct S
{
  int m () const { return 0; } // { dg-message "declared here" }
  int n () const conveyor { return m (); } // { dg-error "not declared 'conveyor'" }
};
