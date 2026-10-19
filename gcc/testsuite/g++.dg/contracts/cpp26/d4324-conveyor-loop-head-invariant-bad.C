// D4324/P2680 item 8: a loop's body is checked with what holds on every
// iteration (the loop head's facts), not with what held before the loop
// -- TOTAL is zero only on the first iteration, I non-negative only
// until it has been decremented.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int accumulate (int i) conveyor
{
  if (i < 0)
    return 0;
  int total = 0;
  for (; i < 2000000000; i++)
    total += i; // { dg-error "result of .\\(total \\+ i\\). not provably free of overflow" }
  return total;
}

int doubling (int n) conveyor
{
  int x = 1;
  for (int i = 0; i < n; i++)
    x = x * 2; // { dg-error "result of .\\(x \\* 2\\). not provably free of overflow" }
  return x;
}

int down (int i) conveyor
{
  int a[10] = {};
  if (i < 0)
    return 0;
  int s = 0;
  for (; i < 10; i -= 3) // { dg-error "result of \[^\n\]* not provably free of overflow" }
    s = a[i]; // { dg-error "array index .i. not provably in-bounds" }
  return s;
}

int down_by_one (int i) conveyor
{
  if (i < 0)
    return 0;
  for (; i < 10; )
    i--; // { dg-error "decrement of .i. not provably free of overflow" }
  return i;
}

int counted (int n) conveyor
{
  int a[10] = {};
  int s = 0;
  for (int i = 0; i < n && i < 10; i++)
    s = a[i];
  return s;
}
