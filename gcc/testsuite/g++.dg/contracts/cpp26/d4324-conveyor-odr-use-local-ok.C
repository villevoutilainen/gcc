// D4324, Increment P: an ordinary local variable or parameter is never
// restricted by the odr-use rule -- only namespace/class-scope and
// thread_local variables are.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int p) conveyor
{
  int local = p;
  return local;
}

int main () { return f (7) - 7; }
