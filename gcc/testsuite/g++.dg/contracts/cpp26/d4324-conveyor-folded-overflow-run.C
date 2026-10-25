// D4324: folding a checked function as if overflow trapped
// (d4324-conveyor-folded-overflow-bad.C) is analysis only: the
// functions still compute the same results when optimized.
// { dg-do run { target c++26 } }
// { dg-additional-options "-O2 -fcontracts -fcontract-control-objects" }

#include <climits>

bool a (int x) conveyor { return x > 0 && INT_MAX - x > 5; }
bool b (int x) conveyor { return x <= 0 || INT_MAX - x > 5; }

int
main ()
{
  int vals[] = { INT_MIN, -7, -1, 0, 1, 5, INT_MAX - 6, INT_MAX - 5,
		 INT_MAX - 4, INT_MAX };
  for (int v : vals)
    {
      bool ea = v > 0 && (long long) INT_MAX - v > 5;
      bool eb = v <= 0 || (long long) INT_MAX - v > 5;
      if (a (v) != ea || b (v) != eb)
	__builtin_abort ();
    }
}
