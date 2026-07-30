// D4324: with _GLIBCXX_ASSERTIONS, -fcontracts and
// -fcontract-control-objects all active, bits/c++config's own
// __glibcxx_assert macro (exercised here indirectly through
// std::vector::operator[]'s existing bounds check, unchanged itself)
// genuinely routes through contract_assert<noexcept_assert_v>: defining
// _GLIBCXX_ASSERTIONS_USE_VIOLATION_HANDLER and a replaceable
// ::handle_contract_violation proves this unambiguously, since the old,
// hardcoded __glibcxx_assert_fail path has no way to reach a contracts
// violation handler at all.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#define _GLIBCXX_ASSERTIONS
#define _GLIBCXX_ASSERTIONS_USE_VIOLATION_HANDLER
#include <vector>
#include <cstdlib>

namespace sc = std::contracts;

void
handle_contract_violation (const sc::contract_violation& v)
{
  bool ok = v.semantic () == sc::evaluation_semantic::enforce
	    && __builtin_strstr (v.comment (), "size") != nullptr;
  std::exit (ok ? 0 : 1);
}

int main ()
{
  std::vector<int> v{1, 2, 3};
  return v[10]; // out of bounds -- must reach the handler above
}
