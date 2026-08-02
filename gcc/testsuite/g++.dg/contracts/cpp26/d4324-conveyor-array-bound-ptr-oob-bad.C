// D4324/P2680 item 8, Increment E2: a pointer's tracked offset that
// provably runs out of the named array's bound (via constant pointer
// arithmetic) must be rejected at the point of dereference.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f () conveyor
{
  int arr[10] = {};
  int* p = &arr[9];
  p = p + 2;
  return *p; // { dg-error "pointer dereference out of bounds in a conveyor function" }
}

int main () { return f (); }
