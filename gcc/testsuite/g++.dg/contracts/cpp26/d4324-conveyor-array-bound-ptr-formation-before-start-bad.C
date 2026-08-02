// D4324/P2680 item 8, Increment W: forming a pointer before an array's
// own start is not well-defined to form, rejected at the point of
// formation; the subsequent dereference is separately (and also
// correctly) rejected by the pre-existing dereference-time check.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

const int arr[5] = { 1, 2, 3, 4, 5 };

int f () conveyor
{
  const int* p = &arr[0];
  const int* q = p - 1; // { dg-error "pointer arithmetic out of bounds in a conveyor function" }
  return *q; // { dg-error "pointer dereference out of bounds in a conveyor function" }
}

int main () { return f (); }
