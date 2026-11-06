// D4324: converting a floating value to an integer type is undefined
// unless, truncated, it is representable ([conv.fpint]): in a conveyor
// function its range must be proven, NaN excluded (P4445 BUG-13).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

int bad (double d) conveyor { return (int) d; } // { dg-error "conversion of 'd' to 'int' not provably in range" }
int bad_hi (double d) conveyor
  pre<conveyor_assert_v> (d < 1e10)
{ return (int) d; } // { dg-error "conversion of 'd' to 'int' not provably in range" }
long implicit (float f) conveyor { long l = f; return l; } // { dg-error "not provably in range" }

int ok (double d) conveyor
  pre<conveyor_assert_v> (d > -1000.0 && d < 1000.0)
{ return (int) d; }
int ok_branch (double d) conveyor
{ if (d > -1000.0 && d < 1000.0) return (int) d; return 0; }
unsigned ok_unsigned (float f) conveyor
  pre<conveyor_assert_v> (f >= 0.0f && f < 100.0f)
{ return f; }
// Truncation: (-1, 256) all convert to an unsigned char.
unsigned char ok_edges (double d) conveyor
  pre<conveyor_assert_v> (d > -1.0 && d < 256.0)
{ return d; }
