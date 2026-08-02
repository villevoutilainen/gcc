// D4324/P2680 item 8, Increment K: a later conjunct's array access now
// benefits from bounds established by earlier conjuncts within the
// very same '&&'-chain -- previously the whole condition was scanned
// eagerly against the pre-condition env, before any refinement from
// its own earlier conjuncts had been applied.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

const int arr[5] = {};

int f (int k) conveyor
{
  const int* p = arr;
  if (k >= 0 && k < 5 && (p = &arr[k]) != nullptr)
    return *p;
  return 0;
}

int main () { return f (2); }
