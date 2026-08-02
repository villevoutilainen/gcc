// D4324/P2680 item 8, Increment N: an array access reached as a
// sub-expression of a loop's own condition (not the whole condition's
// own top-level code) is now validated too, with i bounded by a
// preceding guard -- distinct from the pre-existing Increment E3 test,
// which exercises the condition's *own* comparison refining the
// index range used inside the *body*, not a validation reached
// directly within the condition itself.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int arr[5];

int f (int i) conveyor
{
  int sum = 0;
  if (i < 0 || i >= 5)
    return 0;
  for (; i < 5 && arr[i] >= 0; i++)
    sum++;
  return sum;
}

int main () { return f (0) == 5 ? 0 : 1; }
