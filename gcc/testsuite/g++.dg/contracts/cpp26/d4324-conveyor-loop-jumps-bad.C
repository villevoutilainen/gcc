// D4324/P2680 item 8: what a continue statement leaves reaches the next
// iteration, what a break statement leaves (anywhere in a switch's
// body) the code after it, and a do-while loop's body first runs before
// its condition is tested.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int skip (int n) conveyor
{
  int x = 1;
  for (int i = 0; i < n; i++)
    {
      if (i == 3)
	{
	  x = 0;
	  continue;
	}
      x = 10 / x; // { dg-error "divisor .x. not provably nonzero" }
    }
  return x;
}

int nested_break (int k, int c) conveyor
{
  int x = 1;
  switch (k)
    {
    case 1:
      if (c)
	{
	  x = 0;
	  break;
	}
      x = 2;
      break;
    default:
      break;
    }
  return 10 / x; // { dg-error "divisor .x. not provably nonzero" }
}

int first_iteration (int i) conveyor
{
  int a[10] = {};
  int s = 0;
  if (i < 0)
    return 0;
  do
    s = a[i]; // { dg-error "array index .i. not provably in-bounds" }
  while (++i < 10); // { dg-error "increment of .i. not provably free of overflow" }
  return s;
}
