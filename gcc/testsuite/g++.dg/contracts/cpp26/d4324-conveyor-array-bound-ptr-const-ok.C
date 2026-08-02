// D4324/P2680 item 8, Increment E2: a pointer's tracked offset into a
// named array, established via '&arr[K]' and shifted by constant
// pointer arithmetic, is validated against that array's own bound --
// closing the narrow version's gap where only a direct 'arr[i]' at the
// exact same syntactic point as the array was checked.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f () conveyor
{
  int arr[10] = {};
  int* p = &arr[0];
  p = p + 2;
  return *p;
}

int main () { return f (); }
