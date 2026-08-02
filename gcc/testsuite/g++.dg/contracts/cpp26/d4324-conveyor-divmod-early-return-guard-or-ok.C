// D4324/P2680 item 8, Increment J: the div/mod analogue of the
// ||-conjunct decomposition fix -- 'n <= 0 || n > 1000000' negates
// (De Morgan's) to 'n > 0 && n <= 1000000', excluding zero.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int n) conveyor
{
  if (n <= 0 || n > 1000000)
    return 0;
  return 10 / n;
}

int main () { return f (5) - 2; }
