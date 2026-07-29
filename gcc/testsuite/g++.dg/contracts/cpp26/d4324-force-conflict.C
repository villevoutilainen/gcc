// D4324: force_client_side_check and force_definition_side_check are
// mutually exclusive on the same control type -- a control object can
// only be pinned to one side.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

struct conflicted_probe {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool force_client_side_check (sc::assertion_static_info) { return true; }
  static constexpr bool force_definition_side_check (sc::assertion_static_info) { return true; }
  void
  operator() (const sc::assertion_context& ctx) const
  { ctx.check (); }
};

inline constexpr conflicted_probe conflicted_probe_v{};

int f (int x) pre<conflicted_probe_v>(x >= 0) { return x; } // { dg-error "has both" }
