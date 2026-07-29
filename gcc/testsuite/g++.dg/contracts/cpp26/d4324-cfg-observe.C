// D4324: the TU evaluation_semantic selected by -fcontract-evaluation-semantic
// must reach a control type's compile-time is_ignored(semantic) as the matching
// std::contracts::evaluation_semantic value (ignore=1, observe=2, enforce=3,
// quick_enforce=4).  This is a compile-only proof of the cmdline -> semantic
// mapping that does not need the runtime library.
//
// if_observe::is_ignored is false only when semantic == observe; if_enforce's
// is false only when semantic == enforce.  Built with =observe, the
// observe-keyed assertion must stay active (predicate evaluated, operator()
// called) while the enforce-keyed assertion must be ignored (predicate never
// evaluated, no call).  If the mapping delivered the wrong value (e.g.
// enforce's 3 for observe) the two would swap and the scans below would fail.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe -fdump-tree-gimple" }

#include <contracts>

namespace sc = std::contracts;

bool pred_obs ();
bool pred_enf ();

struct if_observe {
  static constexpr bool is_ignored (sc::assertion_static_info info)
  { return info.semantic () != sc::evaluation_semantic::observe; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void operator() (const sc::assertion_context& ctx) const
  { ctx.check (); }
};

struct if_enforce {
  static constexpr bool is_ignored (sc::assertion_static_info info)
  { return info.semantic () != sc::evaluation_semantic::enforce; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void operator() (const sc::assertion_context& ctx) const
  { ctx.check (); }
};

inline constexpr if_observe if_observe_v{};
inline constexpr if_enforce if_enforce_v{};

int f (int x) pre<if_observe_v>(pred_obs ()) pre<if_enforce_v>(pred_enf ())
{ return x; }

// semantic == observe: the observe-keyed assertion is active.
// { dg-final { scan-tree-dump "pred_obs" "gimple" } }
// { dg-final { scan-tree-dump "if_observe::operator" "gimple" } }
// semantic != enforce: the enforce-keyed assertion is ignored, so its
// predicate is never evaluated and its control is never called.
// { dg-final { scan-tree-dump-not "pred_enf" "gimple" } }
// { dg-final { scan-tree-dump-not "if_enforce::operator" "gimple" } }
