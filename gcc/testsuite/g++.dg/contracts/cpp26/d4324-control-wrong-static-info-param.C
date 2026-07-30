// D4324: a control type whose is_ignored (or any of the other
// assertion_static_info-taking query members) has some other parameter
// type entirely (here, plain evaluation_semantic instead of
// assertion_static_info) must not crash the compiler --
// contract_control_bool_member's own contract already promises to
// treat this exactly like "no usable compile-time member of this
// name" (returns -1), but used to instead unconditionally treat the
// found parameter's type as assertion_static_info's own class type,
// crashing in build_assertion_static_info_value's TYPE_FIELDS on
// anything that isn't a class type (PR-equivalent regression test;
// this exact shape used to ICE: "tree check: expected record_type or
// union_type or qual_union_type, have enumeral_type").
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=ignore" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int calls = 0;

struct bad_probe {
  // Wrong parameter type: assertion_static_info is required here, not
  // evaluation_semantic. Never usable as a compile-time query member,
  // so the contract stays fully active regardless of this TU's
  // -fcontract-evaluation-semantic=.
  static constexpr bool
  is_ignored (sc::evaluation_semantic semantic) noexcept
  { return semantic == sc::evaluation_semantic::ignore; }

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
