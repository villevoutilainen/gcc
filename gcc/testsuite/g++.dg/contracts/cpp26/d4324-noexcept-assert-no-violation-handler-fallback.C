// D4324: _GLIBCXX_ASSERTIONS_USE_VIOLATION_HANDLER's effect requires
// _GLIBCXX_ASSERTIONS too -- with only the former left undefined here,
// noexcept_assert must take the plain __glibcxx_assert_fail-based
// fallback path, not the violation-handler one, reconstructing the
// usual file/line/function/condition-text diagnostic.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-shouldfail "falls back to __glibcxx_assert_fail without the violation-handler macro" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#define _GLIBCXX_ASSERTIONS
#include <contracts>

namespace sc = std::contracts;

int f (int x)
{
  contract_assert<sc::noexcept_assert("fallback message")>(x >= 0);
  return x;
}

int main () { return f (-1); }

// { dg-output "Assertion 'fallback message' failed" }
