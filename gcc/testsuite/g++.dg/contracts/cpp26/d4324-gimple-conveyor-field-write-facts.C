// D4324: in the GIMPLE engine, storing a value to a field establishes
// the field's range and nonzero-ness from the value's, so a function's
// own postcondition about the field can be proven, as in the AST engine.
// A later store replaces the fact.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple -fcontract-conveyor-proofs" }

#include <contracts>
using namespace std::contracts;

struct M
{
  int v;
  explicit M (int x) conveyor
    pre<conveyor_assert_v> (x > 0)
    post<conveyor_assert_v> (v > 0)
  { v = x; }

  void set_constant () conveyor post<conveyor_assert_v> (v == 7) { v = 7; }

  void set_either (int x) conveyor post<conveyor_assert_v> (v > 2)
  { v = x > 5 ? x : 9; }

  void overwritten (int x) conveyor post<conveyor_assert_v> (v > 2) // { dg-warning "cannot verify postcondition condition" }
  { v = 9; v = x; }

  int divide (int y) conveyor
  {
    v = 4;
    return y / v;
  }
};
