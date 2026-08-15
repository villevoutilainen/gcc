// D4324: same as d4324-control-wrong-static-info-param.C, but for a
// *non-static* is_ignored with the wrong parameter type.  Non-static
// query members now go through FUNCTION_FIRST_USER_PARMTYPE (skipping
// the implicit 'this' entry that a METHOD_TYPE's TYPE_ARG_TYPES has, and
// a plain FUNCTION_TYPE doesn't) rather than TYPE_ARG_TYPES directly;
// this exercises that this still correctly identifies the (wrong,
// unusable) parameter type instead of misreading the implicit 'this'
// entry itself as the parameter, and gracefully returns -1 ("no usable
// compile-time member") rather than crashing or misbehaving.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=ignore" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int calls = 0;

struct bad_probe {
  // Wrong parameter type, and non-static: never usable as a
  // compile-time query member, so the contract stays fully active
  // regardless of this TU's -fcontract-evaluation-semantic=.
  constexpr bool
  is_ignored (sc::evaluation_semantic semantic) const noexcept
  { return semantic == sc::evaluation_semantic::ignore; }

  constexpr bool constify (sc::assertion_static_info) const { return false; }
  constexpr bool assumable (sc::assertion_static_info) const { return false; }

  void
  operator() (const sc::assertion_context& ctx) const
  {
    calls++;
    if (!ctx.check ())
      __builtin_abort ();
  }
};

inline constexpr bad_probe bad_probe_v{};

int f (int x) pre<bad_probe_v>(x >= 0) { return x; }

int main ()
{
  int r = f (5);
  if (r != 5)
    __builtin_abort ();
  if (calls != 1)
    __builtin_abort ();
  return 0;
}
