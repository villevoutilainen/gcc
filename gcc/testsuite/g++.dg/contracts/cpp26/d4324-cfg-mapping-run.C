// D4324: end-to-end check that -fcontract-evaluation-semantic=observe is
// delivered to the control object's operator() as the matching
// std::contracts::evaluation_config value at run time.  The control captures
// the cfg it is called with; main verifies it equals evaluation_config::observe.
// A regression in the cmdline -> cfg mapping (contract_evaluation_config_value)
// would make the captured value wrong and abort.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

// Sentinel distinct from observe so a missed call is also caught.
sc::evaluation_config seen = sc::evaluation_config::quick_enforce;
bool called = false;

struct capture {
  static constexpr bool is_ignored (sc::evaluation_config) { return false; }
  static constexpr bool constify = false;
  static constexpr bool assumable = false;
  void
  operator() (const char *, std::source_location, sc::evaluation_config c) const
  { seen = c; called = true; }		// returns -> continue
};

inline constexpr capture capture_v{};

int f (int x) pre<capture_v>(x > 0) { return x; }

int main ()
{
  f (-1);				// violates the precondition
  if (!called)
    __builtin_abort ();
  if (seen != sc::evaluation_config::observe)
    __builtin_abort ();
  return 0;
}
