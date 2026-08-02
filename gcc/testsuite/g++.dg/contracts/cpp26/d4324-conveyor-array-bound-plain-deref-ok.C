// D4324/P2680 item 8, Increment E2: an ordinary pointer dereference
// with no tracked array-offset fact at all must not be flagged by the
// array-bound rule -- that's not what this rule is about (a plain
// pointer's own basic validity is is_object_address's separate,
// unrelated concern).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor
{
  int a = x;
  int* p = &a;
  return *p;
}

int main () { return f (1) - 1; }
