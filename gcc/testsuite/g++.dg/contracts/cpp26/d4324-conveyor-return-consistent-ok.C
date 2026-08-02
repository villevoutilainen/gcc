// D4324, Increment O: both branches return -- must still be accepted
// by the now-real "all exit paths return" check.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (bool flag) conveyor
{
  if (flag)
    return 1;
  else
    return 2;
}

int main () { return f (true) - 1; }
