// D4324: a divisor computed from values ('i + 1', 'i * 3 - 5', '-i') is
// nonzero when the range its operands' facts give it excludes zero.
// Only a bare variable's range was consulted, so all of these were
// rejected; the GIMPLE engine also missed a range a precondition gives,
// which the ranger doesn't know.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
using std::contracts::conveyor_assert_v;

int precondition (int i) conveyor pre<conveyor_assert_v> (i >= 0 && i < 100)
{ return 100 / (i + 1); }

int guarded (int i) conveyor
{ if (i >= 0 && i < 100) return 100 / (i + 1); return 0; }

int product (int i) conveyor
{ if (i >= 2 && i < 100) return 100 % (i * 3 - 5); return 0; }

int negated (int i) conveyor
{ if (i > -100 && i < 0) return 100 / -i; return 0; }

int loop (int n) conveyor
{
  int s = 0;
  for (int i = 0; i < n && i < 100; ++i)
    s = 100 / (i + 1);
  return s;
}

unsigned no_wrap (unsigned u) conveyor
{ if (u >= 1 && u < 1000) return 100u / (u + 1u); return 0; }
