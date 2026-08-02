// D4324/P2680 item 8, Increment W: pointer-arithmetic-formation
// checking also works for a *variable* offset (not just a literal
// constant), as long as it's proven in-range by a preceding guard --
// exercises oa_get_range's new NOP_EXPR(MULT_EXPR(index, elt_size))
// recognition, the tree shape a variable pointer offset arrives in
// (confirmed empirically), as opposed to the literal case's own
// already-constant-folded byte count.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

const int arr[5] = { 1, 2, 3, 4, 5 };

int f (int i) conveyor
{
  if (i < 0 || i > 5)
    return 0;
  const int* p = &arr[0];
  const int* q = p + i;
  return q - p;
}

int main () { return f (2) - 2; }
