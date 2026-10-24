// D4324: an operand known only from one side is still bounded on the
// other by its type: with 'x > 0', 'INT_MAX - x' can't overflow (as
// bsls::TimeInterval::isSumValidInt64's 'LLONG_MAX - lhs' after
// 'lhs > 0').  The AST engine required both bounds from facts.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <climits>

int a (int x) conveyor { if (x > 0) return INT_MAX - x; return 0; }
int b (int x) conveyor { if (x <= 0) return INT_MIN - x; return 0; }
long long c (long long x) conveyor { if (x > 0) return LLONG_MAX - x; return 0; }
bool d (long long lhs, long long rhs) conveyor
{
  return (lhs > 0 && LLONG_MAX - lhs >= rhs)
	 || (lhs <= 0 && LLONG_MIN - lhs <= rhs);
}
int e (int x) conveyor { if (x >= 0) return x + INT_MIN; return 0; }
