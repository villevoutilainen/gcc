// D4324: force_client_side_check/force_definition_side_check are a
// per-contract property, not a per-function one. A single function can
// have one precondition forced to the caller side and another left to
// the ordinary policy, and each is dispatched independently: with no
// -fcontracts-client-check/-fcontracts-definition-check option (today's
// defaults: client-check=none, definition-check=on), the forced
// precondition is checked only via the caller-side wrapper (built despite
// client-check defaulting to none) while the plain precondition is
// checked only at f's own definition (the ordinary default-policy
// outcome for an unforced contract).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int forced_calls = 0;
int plain_calls = 0;

struct forced_probe {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify  = false;
  static constexpr bool assumable = false;
  static constexpr bool force_client_side_check = true;
  void
  operator() (const sc::assertion_context& ctx) const
  {
    forced_calls++;
    if (!ctx.check ())
      __builtin_abort ();
  }
};

struct plain_probe {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify  = false;
  static constexpr bool assumable = false;
  void
  operator() (const sc::assertion_context& ctx) const
  {
    plain_calls++;
    if (!ctx.check ())
      __builtin_abort ();
  }
};

inline constexpr forced_probe forced_probe_v{};
inline constexpr plain_probe plain_probe_v{};

int f (int x) pre<forced_probe_v>(x >= 0) pre<plain_probe_v>(x <= 100);

int
f (int x)
{
  return x;
}

int main ()
{
  if (f (5) != 5)
    __builtin_abort ();
  if (forced_calls != 1)
    __builtin_abort ();
  if (plain_calls != 1)
    __builtin_abort ();
  return 0;
}
