// D4324: an enumerator is a constant divisor too ('x /
// k_NANOSECS_PER_SEC', a member of an unscoped enum in a class, as
// bsls::TimeInterval has); the AST engine took it for an unknown one.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct T
{
  enum { k_N = 1000000000 };
  static int d (int x) conveyor { return x / k_N; }
  static int m (int x) conveyor { return x % k_N; }
};

enum E { e_ONE = 1 };
int g (int x) conveyor { return x / e_ONE; }
