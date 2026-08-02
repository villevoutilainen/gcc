// D4324/P2680 item 8, Increment W: pointer subtraction is also checked
// at formation -- even though the variable offset i is guarded into
// [0,5], 'p - i' (as opposed to 'p + i') can still drive the resulting
// offset negative (down to -5), so this must still be rejected.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

const int arr[5] = { 1, 2, 3, 4, 5 };

int f (int i) conveyor
{
  if (i < 0 || i > 5)
    return 0;
  const int* p = &arr[0];
  const int* q = p - i; // { dg-error "pointer arithmetic not provably in-bounds in a conveyor function" }
  return q - p;
}

int main () { return f (0); }
