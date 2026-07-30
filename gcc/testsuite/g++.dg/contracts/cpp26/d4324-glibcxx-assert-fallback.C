// D4324: __glibcxx_assert's rewrite requires *both* -fcontracts and
// -fcontract-control-objects (via __cpp_contracts and
// __cpp_contract_control_objects) -- with only -fcontracts passed here,
// bits/c++config must fall back to the original, unmodified
// __glibcxx_assert_fail path, unchanged.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts" }
// { dg-shouldfail "falls back to __glibcxx_assert_fail without -fcontract-control-objects" }

#define _GLIBCXX_ASSERTIONS
#include <vector>

int main ()
{
  std::vector<int> v{1, 2, 3};
  return v[10]; // out of bounds
}

// { dg-output "Assertion '.*size.*' failed" }
