// D4324: a range-based for's hidden '__for_range' is the language's route
// to the range: its begin () call, not conveyor, doesn't end the object it
// refers to for the end () call that follows (the hidden variables are
// trusted at each use, contract_hidden_var_p).  The GIMPLE engine ended
// the referent through the reference's value, where the AST engine kept it
// -- found with libc++'s vector, whose begin () wasn't conveyor.
// { dg-do run { target c++26 } }
// { dg-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

struct C
{
  int a[2];
  int *begin () { return a; }		// not conveyor
  int *end () conveyor { return a + 2; }
};

int
f (C &c)
  pre<std::contracts::conveyor_assert_v> (std::is_object_address (&c))
{
  int n = 0;
  for (int e : c)
    n += e;
  return n;
}

int
main ()
{
  C c{{1, 2}};
  if (f (c) != 3)
    __builtin_abort ();
}
