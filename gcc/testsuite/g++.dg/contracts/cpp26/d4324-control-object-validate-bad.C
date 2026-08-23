// D4324: a hand-rolled, non-P3400 control object whose validate() folds
// to a contract_validation_result with valid == false is rejected
// directly -- proving validate() is a genuinely general Contract
// Control Object member usable by any control type, not something
// wired specifically for std::contracts::P3400's own internal use (see
// d4324-p3400-dynamic-reject-bad.C for that case). Its own message
// string is folded into the diagnostic.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

struct always_reject {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr sc::contract_validation_result
  validate (sc::assertion_static_info)
  { return { false, "always_reject never accepts anything" }; }
  void
  operator() (const sc::assertion_context& ctx) const
  { if (ctx.check ()) return; __builtin_abort (); }
};
inline constexpr always_reject always_reject_v{};

// { dg-error "rejected this assertion: always_reject never accepts anything" "" { target *-*-* } 0 }
int
f (int x) pre<always_reject_v>(x > 0)
{ return x; }
