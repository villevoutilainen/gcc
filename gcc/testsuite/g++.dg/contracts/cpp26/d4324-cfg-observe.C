// D4324: the TU evaluation_config selected by -fcontract-evaluation-semantic
// must reach a control type's compile-time is_ignored(cfg) as the matching
// std::contracts::evaluation_config value (ignore=0, observe=1, enforce=2,
// quick_enforce=3).  This is a compile-only proof of the cmdline -> cfg
// mapping that does not need the runtime library.
//
// if_observe::is_ignored is false only when cfg == observe; if_enforce's is
// false only when cfg == enforce.  Built with =observe, the observe-keyed
// assertion must stay active (predicate evaluated, operator() called) while
// the enforce-keyed assertion must be ignored (predicate never evaluated, no
// call).  If the mapping delivered the wrong value (e.g. enforce's 2 for
// observe) the two would swap and the scans below would fail.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe -fdump-tree-gimple" }

namespace std {
struct source_location {
  constexpr source_location () = default;
};
namespace contracts {
enum class evaluation_config : unsigned {
  ignore = 0, observe = 1, enforce = 2, quick_enforce = 3
};
}
}
namespace sc = std::contracts;

bool pred_obs ();
bool pred_enf ();

struct if_observe {
  static constexpr bool is_ignored (sc::evaluation_config c)
  { return c != sc::evaluation_config::observe; }
  static constexpr bool constify = false;
  static constexpr bool assumable = false;
  void operator() (const char *, std::source_location,
		   sc::evaluation_config, void* args,
		   bool (*check) (void*)) const { check (args); }
};

struct if_enforce {
  static constexpr bool is_ignored (sc::evaluation_config c)
  { return c != sc::evaluation_config::enforce; }
  static constexpr bool constify = false;
  static constexpr bool assumable = false;
  void operator() (const char *, std::source_location,
		   sc::evaluation_config, void* args,
		   bool (*check) (void*)) const { check (args); }
};

inline constexpr if_observe if_observe_v{};
inline constexpr if_enforce if_enforce_v{};

int f (int x) pre<if_observe_v>(pred_obs ()) pre<if_enforce_v>(pred_enf ())
{ return x; }

// cfg == observe: the observe-keyed assertion is active.
// { dg-final { scan-tree-dump "pred_obs" "gimple" } }
// { dg-final { scan-tree-dump "if_observe::operator" "gimple" } }
// cfg != enforce: the enforce-keyed assertion is ignored, so its predicate is
// never evaluated and its control is never called.
// { dg-final { scan-tree-dump-not "pred_enf" "gimple" } }
// { dg-final { scan-tree-dump-not "if_enforce::operator" "gimple" } }
