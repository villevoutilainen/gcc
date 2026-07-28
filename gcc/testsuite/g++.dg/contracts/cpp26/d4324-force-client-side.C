// D4324: force_client_side_check pins a contract to the caller-side
// (client) wrapper regardless of the command-line policy: no
// -fcontracts-client-check/-fcontracts-definition-check option is given
// at all here, i.e. today's defaults (client-check=none,
// definition-check=on) -- which, absent the flag, would mean the control
// object is checked only at f's own definition, never via a wrapper. With
// force_client_side_check the opposite happens: a wrapper is still built
// and used despite -fcontracts-client-check defaulting to none, and f's
// own definition does *not* also check it (unlike the ordinary policy's
// intentional double-check overlap, see callerside-checks-pre.C).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int pre_calls = 0;
int post_calls = 0;

struct probe {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify  = false;
  static constexpr bool assumable = false;
  static constexpr bool force_client_side_check = true;
  void
  operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.kind () == sc::assertion_kind::pre)
      pre_calls++;
    else if (ctx.kind () == sc::assertion_kind::post)
      post_calls++;
    if (!ctx.check ())
      __builtin_abort ();
  }
};

inline constexpr probe probe_v{};

int f (int x) pre<probe_v>(x >= 0) post<probe_v>(r: r >= 0);

int
f (int x)
{
  return x;
}

int main ()
{
  if (f (1) != 1)
    __builtin_abort ();
  if (pre_calls != 1 || post_calls != 1)
    __builtin_abort ();
  return 0;
}
