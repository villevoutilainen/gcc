// D4324: a merely *absent* optional trait (no omit_comment/
// omit_source_location/etc. declared at all) must keep compiling
// clean under the loud-on-broken-trait fix -- only a *present but
// broken* trait is diagnosed (see d4324-control-trait-broken-*-bad.C).
// This guards that the two cases stay distinguishable.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int calls = 0;

struct no_optional_traits {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  // Deliberately no omit_comment/omit_source_location/
  // force_client_side_check/force_definition_side_check/inherited/
  // is_conveyor/is_symbolic -- all legitimately absent.

  void
  operator() (const sc::assertion_context& ctx) const
  {
    calls++;
    if (!ctx.check ())
      __builtin_abort ();
  }
};

inline constexpr no_optional_traits no_optional_traits_v{};

int f (int x) pre<no_optional_traits_v>(x >= 0) { return x; }

int main ()
{
  int r = f (5);
  if (r != 5)
    __builtin_abort ();
  if (calls != 1)
    __builtin_abort ();
  return 0;
}
