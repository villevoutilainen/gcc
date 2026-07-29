// D4324: force_definition_side_check pins a contract to the function's
// own definition regardless of the command-line policy: here
// -fcontracts-client-check=all -fcontracts-definition-check=off would,
// absent the flag, mean this contract is checked only via a caller-side
// wrapper and never at f's own definition. With force_definition_side_check
// the opposite happens: f's own definition still checks it despite
// -fcontracts-definition-check=off, and no wrapper is ever built or used
// for it despite -fcontracts-client-check=all.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontracts-client-check=all -fcontracts-definition-check=off -fdump-tree-gimple" }

#include <contracts>

namespace sc = std::contracts;

struct probe {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool force_definition_side_check (sc::assertion_static_info) { return true; }
  void
  operator() (const sc::assertion_context& ctx) const
  { ctx.check (); }
};

inline constexpr probe probe_v{};

int f (int x) pre<probe_v>(x >= 0) { return x; }

int g (int x) { return f (x); }

// The control object is called from f's own definition ...
// { dg-final { scan-tree-dump "probe::operator" "gimple" } }
// ... and no caller-side wrapper is ever built for f.
// { dg-final { scan-tree-dump-not "contract_wrapper" "gimple" } }
