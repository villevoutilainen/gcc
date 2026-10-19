// D4324/P2680 item 8: '++' and '--' change what is known about their
// operand, like the assignment they amount to.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int post (int i) conveyor
{
  if (i < 1)
    return 0;
  i--;
  return 10 / i; // { dg-error "divisor .i. not provably nonzero" }
}

int pre (int i) conveyor
{
  if (i < 1)
    return 0;
  --i;
  return 10 / i; // { dg-error "divisor .i. not provably nonzero" }
}

int twice (int i, int n) conveyor
{
  if (i > n)
    {
      i--;
      i--; // { dg-error "decrement of .i. not provably free of overflow" }
    }
  return i;
}

int fine (int i) conveyor
{
  if (i < 0 || i > 100)
    return 0;
  i++;
  return 10 / i;
}
