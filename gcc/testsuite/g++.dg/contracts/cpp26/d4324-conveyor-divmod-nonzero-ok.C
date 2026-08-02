// D4324/P2680 item 8, narrow version: a division/modulo whose divisor
// is a literal nonzero constant, or a decl straight-line-assigned from
// one, is accepted in a conveyor function.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int a) conveyor
{
  int b = 5;
  return a / b;
}

int main () { return f (10) - 2; }
