// D4324/P2680 item 8: after a loop, its condition is known to have
// failed.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int do_while (int i) conveyor
{
  if (i < 0 || i > 5)
    return 0;
  do
    i++;
  while (i < 10);
  return 100 / i;
}

int while_loop (int i) conveyor
{
  if (i < 0)
    return 0;
  if (i > 5)
    return 0;
  while (i < 10)
    i++;
  return 100 / i;
}

int main ()
{
  return do_while (3) + while_loop (3) == 20 ? 0 : 1;
}
