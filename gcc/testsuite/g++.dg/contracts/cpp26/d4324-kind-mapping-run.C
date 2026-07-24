// D4324: end-to-end check that assertion_context::kind() reports the right
// std::contracts::assertion_kind at run time for a precondition, a
// postcondition, and a contract_assert, each naming the same control object.
// A regression in how CONTRACT_ASSERTION_KIND is threaded into
// build_contract_control_call would make a captured value wrong and abort.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

// Sentinels distinct from the expected value, so a missed call is also caught.
sc::assertion_kind seen_pre = sc::assertion_kind::assert;
sc::assertion_kind seen_post = sc::assertion_kind::assert;
sc::assertion_kind seen_assert = sc::assertion_kind::pre;

struct capture {
  static constexpr bool is_ignored (sc::evaluation_config) { return false; }
  static constexpr bool constify = false;
  static constexpr bool assumable = false;
  void
  operator() (const sc::assertion_context& ctx) const
  {
    switch (ctx.kind ())
      {
      case sc::assertion_kind::pre:    seen_pre = ctx.kind ();    break;
      case sc::assertion_kind::post:   seen_post = ctx.kind ();   break;
      case sc::assertion_kind::assert: seen_assert = ctx.kind (); break;
      default: __builtin_abort ();
      }
    if (!ctx.check ())
      __builtin_abort ();		// every predicate here holds
  }
};

inline constexpr capture capture_v{};

int f (int x) pre<capture_v>(x > 0) post<capture_v>(r: r > 0) { return x; }

void g (int x)
{
  contract_assert<capture_v>(x > 0);
}

int main ()
{
  f (1);
  g (1);
  if (seen_pre != sc::assertion_kind::pre)
    __builtin_abort ();
  if (seen_post != sc::assertion_kind::post)
    __builtin_abort ();
  if (seen_assert != sc::assertion_kind::assert)
    __builtin_abort ();
  return 0;
}
