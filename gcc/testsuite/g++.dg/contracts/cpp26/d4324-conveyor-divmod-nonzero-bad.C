// D4324/P2680 item 8, narrow version: a division/modulo whose divisor
// is an ordinary parameter, with no provable nonzero-ness at all (this
// narrow pass does no cross-statement inference beyond a literal
// constant or a straight-line assignment from one -- see the plan's
// Increment E for real dataflow parity with is_object_address), must be
// rejected in a conveyor function.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int a, int b) conveyor
{
  return a / b; // { dg-error "divisor .b. not provably nonzero in a conveyor function" }
}

int main () { return f (10, 2) - 5; }
