// D4324/P2680 item 8, narrowest useful version of the pointer-
// arithmetic array-bound rule: a compile-time-constant index that is
// actually out of bounds is rejected in a conveyor function.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f () conveyor
{
  int arr[3] = {1, 2, 3};
  return arr[5]; // { dg-error "array index .5. out of bounds in a conveyor function" }
}

int main () { return f (); }
