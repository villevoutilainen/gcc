// D4324/P2680 item 8, Increment E3: a loop's own condition refines the
// index range used for the body's array access -- 'i < 5' establishes
// i's upper bound at exactly the array's own last valid index.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f () conveyor
{
  int arr[5] = {};
  int last = 0;
  for (int i = 0; i < 5; ++i)
    last = arr[i];
  return last;
}

int main () { return f (); }
