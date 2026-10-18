// D4324: the GIMPLE engine checks 'is_object_address (p)' for a local
// pointer p in a contract_assert, like the AST engine: provable when
// every path sets it to an object's address, not when one arm leaves an
// unknown pointer.  Only the paths that reach the assert count: a
// pointer set inside a short-circuit condition is the object's address
// where the condition holds.  A never_proven assert establishes it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;

int x;

int one_arm (int c, int *q)
{
  int a = 1;
  int *p;
  if (c > 0)
    p = &a;
  else
    p = q;
  contract_assert<conveyor_assert_v> (std::is_object_address (p)); // { dg-error "cannot prove .is_object_address. for .p." }
  return *p;
}

int both_arms (int c)
{
  int a = 1, b = 2;
  int *p;
  if (c > 0)
    p = &a;
  else
    p = &b;
  contract_assert<conveyor_assert_v> (std::is_object_address (p));
  return *p;
}

int in_condition (bool a, bool b)
{
  int *p = nullptr;
  if (a && (p = &x) != nullptr && b)
    {
      contract_assert<conveyor_assert_v> (std::is_object_address (p));
      return *p;
    }
  return 0;
}

int asserted (int *q)
{
  int *p = q;
  contract_assert<never_proven_conveyor_v> (std::is_object_address (p));
  contract_assert<conveyor_assert_v> (std::is_object_address (p));
  return *p;
}
