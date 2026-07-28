// D4324: force_client_side_check and force_definition_side_check are
// mutually exclusive on the same control type -- a control object can
// only be pinned to one side.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

struct conflicted_probe {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify  = false;
  static constexpr bool assumable = false;
  static constexpr bool force_client_side_check = true;
  static constexpr bool force_definition_side_check = true;
  void
  operator() (const sc::assertion_context& ctx) const
  { ctx.check (); }
};

inline constexpr conflicted_probe conflicted_probe_v{};

int f (int x) pre<conflicted_probe_v>(x >= 0) { return x; } // { dg-error "has both" }
