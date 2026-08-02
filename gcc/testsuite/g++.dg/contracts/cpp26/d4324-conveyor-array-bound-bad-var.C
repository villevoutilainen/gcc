// D4324/P2680 item 8, narrowest useful version of the pointer-
// arithmetic array-bound rule: a non-constant index is rejected outright
// in this narrow pass (no dataflow attempted for indices at all -- see
// the plan's Increment E for real array-bound/size tracking).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int i) conveyor
{
  int arr[3] = {1, 2, 3};
  return arr[i]; // { dg-error "array index .i. not provably in-bounds in a conveyor function" }
}

int main () { return f (1) - 2; }
