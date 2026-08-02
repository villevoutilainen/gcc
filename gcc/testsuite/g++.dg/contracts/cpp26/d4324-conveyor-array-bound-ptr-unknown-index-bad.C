// D4324/P2680 item 8, Increment E2: '&arr[i]' with a runtime-variable
// index that has no established range at all must still be rejected --
// subscript syntax unambiguously signals array access, so an
// unprovable case is always an error, never silently skipped.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int i) conveyor
{
  int arr[10] = {};
  int* p = &arr[i]; // { dg-error "array index .i. not provably in-bounds in a conveyor function" }
  return *p;
}

int main () { return f (0); }
