// D4324: same setup as d4324-glibcxx-assert-new-path.C, but with an
// in-bounds access -- confirms ordinary, non-violating library operation
// is completely unaffected by routing __glibcxx_assert through
// contract_assert<noexcept_assert_v>: the handler must never be called,
// and the container behaves exactly as it always did.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#define _GLIBCXX_ASSERTIONS
#define _GLIBCXX_ASSERTIONS_USE_VIOLATION_HANDLER
#include <vector>
#include <string>
#include <span>

namespace sc = std::contracts;

bool handler_called = false;

void
handle_contract_violation (const sc::contract_violation&)
{
  handler_called = true;
}

int main ()
{
  std::vector<int> v{1, 2, 3};
  std::string s = "hello";
  std::span<int> sp (v);

  int r = v[1] + s[0] + sp[2];

  if (handler_called)
    __builtin_abort ();
  if (r != 2 + 'h' + 3)
    __builtin_abort ();

  return 0;
}
