// D4324/P2680, Increment M: no default: label; a fact established
// before the switch (and never touched by any case) must survive to
// a division after the switch -- confirms the "no case matches" path
// correctly contributes the untouched pre-switch env to the merge.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int flag) conveyor
{
  int n = 5;
  switch (flag)
    {
    case 0:
      return 1;
    case 1:
      return 2;
    }
  return 10 / n;
}

int main () { return f (9) - 2; }
