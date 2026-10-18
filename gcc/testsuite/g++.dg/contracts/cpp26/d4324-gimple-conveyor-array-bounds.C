// D4324: item 8's array bounds in the GIMPLE engine, like the AST
// engine's: an index into a fixed-size array must be provably within it,
// a pointer formed into one ('&arr[i]', 'p + n') may reach one past the
// end, and a dereference of such a pointer must stay within it.  A loop
// counter is bounded by its start and direction as well as by the loop's
// condition.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

const int arr[5] = { 1, 2, 3, 4, 5 };

int constant () conveyor { return arr[2]; }
int constant_bad () conveyor
{
  int local[3] = { 1, 2, 3 };
  return local[5]; // { dg-error "array index .5. out of bounds" }
}
int unknown (int i) conveyor { return arr[i]; } // { dg-error "array index .i. not provably in-bounds" }

int guarded (int i) conveyor
{
  if (i >= 0 && i < 5)
    return arr[i];
  return 0;
}

int loop () conveyor
{
  int last = 0;
  for (int i = 0; i < 5; ++i)
    last = arr[i];
  return last;
}

int pointer (int i) conveyor
{
  if (i >= 0 && i < 4)
    {
      const int *p = &arr[i];
      p = p + 1;
      return *p;
    }
  return 0;
}

int one_past_end () conveyor
{
  const int *e = arr + 5;
  return e - arr;
}

int past_end () conveyor
{
  const int *p = &arr[0];
  const int *e = p + 6; // { dg-error "pointer arithmetic out of bounds" }
  return e - p;
}

int deref_past_end () conveyor
{
  const int *p = &arr[4];
  p = p + 1;
  return *p; // { dg-error "pointer dereference out of bounds" }
}

int unknown_offset (int i) conveyor
{
  const int *p = &arr[0];
  const int *q = p + i; // { dg-error "pointer arithmetic not provably in-bounds" }
  return q - p;
}
