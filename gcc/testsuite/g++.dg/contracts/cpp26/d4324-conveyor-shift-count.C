// D4324: a shift count must be non-negative and less than the width of
// the shifted operand's promoted type in a conveyor function
// (P4445 BUG-11).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -Wno-shift-count-overflow" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

int lit (int a) conveyor { return a << 40; } // { dg-error "shift count '40' not provably non-negative and less than 32" }
unsigned any (unsigned a, unsigned n) conveyor { return a << n; } // { dg-error "shift count 'n' not provably" }
int neg (int a, int n) conveyor
  pre<conveyor_assert_v> (n < 32)
{ return a >> n; } // { dg-error "shift count 'n' not provably" }

unsigned ok (unsigned a, unsigned n) conveyor
  pre<conveyor_assert_v> (n < 32)
{ return a << n; }

int ok_signed (int a, int n) conveyor
  pre<conveyor_assert_v> (n >= 0 && n < 32)
{ return a >> n; }

// An excluded end of the count's range: one narrower.
unsigned ok_end (unsigned a, unsigned n) conveyor
  pre<conveyor_assert_v> (n <= 32)
{
  if (n == 32)
    return 0;
  return a << n;
}

// A char is promoted: the width is int's.
int ok_promoted (signed char c) conveyor { return c << 24; }
