// D4324/P2680 item 8, Increment E3: a pointer's array-offset range,
// reassigned inside a loop via a range-tracked (loop-condition-
// refined) index, merges by union across iterations -- closing a
// latent gap between when Increment E2 started populating this fact
// and E3's proper reasoning: previously a loop-reassigned pointer's
// array-offset fact was never invalidated *or* reasoned about at all.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n) conveyor
{
  int arr[10] = {};
  int* p = &arr[0];
  for (int i = 0; i < n && i < 5; ++i)
    p = &arr[i];
  return *p;
}

int main () { return f (3); }
