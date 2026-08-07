// D4324/P2680: the IF_STMT/COND_EXPR condition-operand gap -- a stray
// std::is_object_address use directly inside an if-condition, outside
// any contract construct entirely, must still be rejected by the
// well-formedness gate, the same as anywhere else a stray use is
// already caught.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int f ()
{
  int x = 5;
  if (std::is_object_address (&x)) // { dg-error "may only be used directly inside a conveyor- or symbolic-checked" }
    return 0;
  return 1;
}

int main () { return f (); }
