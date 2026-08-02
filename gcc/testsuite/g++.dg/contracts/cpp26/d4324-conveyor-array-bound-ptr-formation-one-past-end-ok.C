// D4324/P2680 item 8, Increment W: pointer-arithmetic-*formation*
// checking -- forming a one-past-the-end pointer (offset == N for an
// N-element array) is well-defined even though dereferencing it is
// not, so this must be accepted (the array-bound rule's own upper
// bound for mere formation is N, one more than the strict N-1 bound
// used at actual access).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

const int arr[5] = { 1, 2, 3, 4, 5 };

int f () conveyor
{
  const int* p = &arr[0];
  const int* q = p + 5;
  return q - p;
}

int main () { return f () - 5; }
