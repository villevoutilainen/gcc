// D4324/P2680 item 8, Increment K: the mirror-image ordering --
// '&arr[k]' as the *first* conjunct, with the bounds check only
// coming *afterward* -- must stay rejected: '&&' evaluates left to
// right, so the array access genuinely executes before k's bounds
// are checked. Confirms the fix is order-sensitive, not a blanket
// relaxation (this is the shape Increment G's own writeup originally,
// incorrectly, cited as "the gap").
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int arr[5];

int f (int k) conveyor
{
  int* p = arr;
  if ((p = &arr[k]) != nullptr && k >= 0 && k < 5) // { dg-error "array index .k. not provably in-bounds in a conveyor function" }
    return *p;
  return 0;
}

int main () { return f (2); }
