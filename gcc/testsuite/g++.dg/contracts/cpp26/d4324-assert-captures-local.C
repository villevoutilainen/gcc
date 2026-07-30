// D4324: a contract_assert (unlike pre/post, free to reference any
// local variable in scope, not just the enclosing function's own
// parameters) with a named control object still has its predicate
// outlined into a standalone function (build_predicate_core_function_1)
// so ctx.check()'s callback can invoke it on demand. The outliner must
// capture any local the condition references beyond the function's own
// parameters -- this used to ICE (dangling reference to a local that
// doesn't exist in the outlined function) for exactly this shape.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

int calls = 0;

struct probe {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    calls++;
    if (!ctx.check ())
      __builtin_abort ();
  }
};
inline constexpr probe probe_v{};

int
find_index (int x)
{
  // i/found are locals computed above the assertion, not parameters --
  // the outlined predicate function must capture both to reference them.
  int i = x * 2;
  bool found = (i >= 0);
  contract_assert<probe_v>(found && i == x * 2);
  return i;
}

int main ()
{
  int r = find_index (21);
  if (r != 42)
    __builtin_abort ();
  if (calls != 1)
    __builtin_abort ();
  return 0;
}
