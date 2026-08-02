// D4324/P2680 item 8, Increment E2: a pointer's tracked offset that
// provably runs out of the named array's bound (via constant pointer
// arithmetic) must be rejected at the point of dereference. Since
// Increment W, it is also rejected earlier, at the point the
// out-of-bounds pointer is *formed* (offset 11 is out of range even
// for mere formation, since the array only has 10 elements, so even
// one-past-the-end -- offset 10 -- would be the most that's formable).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f () conveyor
{
  int arr[10] = {};
  int* p = &arr[9];
  p = p + 2; // { dg-error "pointer arithmetic out of bounds in a conveyor function" }
  return *p; // { dg-error "pointer dereference out of bounds in a conveyor function" }
}

int main () { return f (); }
