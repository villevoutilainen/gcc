// D4324: an ordinary conveyor function with no restricted constructs
// compiles and runs exactly like a normal function -- 'conveyor' is
// declaration-only, with no ABI or codegen impact.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor
{
  int y = x + 1;
  return y;
}

int main () { return f (1) - 2; }
