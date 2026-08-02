// D4324/P2680 item 8, narrowest useful version of the pointer-
// arithmetic array-bound rule: an ARRAY_REF on a fixed-size array with
// a compile-time-constant index actually within bounds is accepted in
// a conveyor function.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f () conveyor
{
  int arr[3] = {1, 2, 3};
  return arr[1];
}

int main () { return f () - 2; }
