// D4324/P2680 item 8, Increment W: forming a pointer *beyond*
// one-past-the-end (offset == N+1 for an N-element array) is not
// well-defined even to form, so it must be rejected at the point of
// formation -- before any dereference is even attempted.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

const int arr[5] = { 1, 2, 3, 4, 5 };

int f () conveyor
{
  const int* p = &arr[0];
  const int* q = p + 6; // { dg-error "pointer arithmetic out of bounds in a conveyor function" }
  return q - p;
}

int main () { return f () - 6; }
