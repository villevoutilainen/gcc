// D4324/P2680 item 8, Increment E2: full symbolic range analysis for
// the array-bound rule -- a runtime-variable index proven in-range by
// a preceding comparison (Increment E1) validates '&arr[i]', and the
// resulting pointer's tracked offset is further shifted by pointer
// arithmetic and still validated correctly.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int i) conveyor
{
  int arr[10] = {};
  if (i >= 0 && i < 8)
    {
      int* p = &arr[i];
      p = p + 1;
      return *p;
    }
  return 0;
}

int main () { return f (3) - 0; }
