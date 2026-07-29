// D4324: assertion_context::static_info().side() tells a control object,
// at the point operator() actually runs, whether this occurrence is being
// checked at the function's own definition, via a caller-side wrapper, or
// neither applies (a contract_assert, which has no caller/definition
// distinction at all). force_client_side_check/force_definition_side_check
// pin f's/h's contracts to one side each, so which side() value shows up
// is fully determined, not a race between the ordinary command-line
// policy and whichever side happens to run first.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

sc::assertion_check_side seen_side_client = sc::assertion_check_side::not_applicable;
sc::assertion_check_side seen_side_definition = sc::assertion_check_side::not_applicable;
sc::assertion_check_side seen_side_assert = sc::assertion_check_side::not_applicable;

struct probe {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool force_client_side_check (sc::assertion_static_info) { return true; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    seen_side_client = ctx.static_info ().side ();
    if (!ctx.check ())
      __builtin_abort ();
  }
};

struct probe_def {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool force_definition_side_check (sc::assertion_static_info) { return true; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    seen_side_definition = ctx.static_info ().side ();
    if (!ctx.check ())
      __builtin_abort ();
  }
};

struct probe_assert {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    seen_side_assert = ctx.static_info ().side ();
    if (!ctx.check ())
      __builtin_abort ();
  }
};

inline constexpr probe probe_v{};
inline constexpr probe_def probe_def_v{};
inline constexpr probe_assert probe_assert_v{};

int f (int x) pre<probe_v>(x >= 0);

int
f (int x)
{
  return x;
}

int h (int x) pre<probe_def_v>(x >= 0) { return x; }

void
k (int x)
{
  contract_assert<probe_assert_v>(x >= 0);
}

int main ()
{
  if (f (1) != 1)
    __builtin_abort ();
  if (seen_side_client != sc::assertion_check_side::client)
    __builtin_abort ();

  if (h (1) != 1)
    __builtin_abort ();
  if (seen_side_definition != sc::assertion_check_side::definition)
    __builtin_abort ();

  k (1);
  if (seen_side_assert != sc::assertion_check_side::not_applicable)
    __builtin_abort ();

  return 0;
}
