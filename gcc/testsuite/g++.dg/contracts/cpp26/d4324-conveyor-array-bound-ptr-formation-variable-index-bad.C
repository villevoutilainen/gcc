// D4324/P2680 item 8, Increment W: the same variable-offset formation
// check, but with no guard establishing the index's range at all --
// must still be rejected (confirming the check actually fires, not
// just silently accepting any variable offset).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

const int arr[5] = { 1, 2, 3, 4, 5 };

int f (int i) conveyor
{
  const int* p = &arr[0];
  const int* q = p + i; // { dg-error "pointer arithmetic not provably in-bounds in a conveyor function" }
  return q - p;
}

int main () { return f (2) - 2; }
