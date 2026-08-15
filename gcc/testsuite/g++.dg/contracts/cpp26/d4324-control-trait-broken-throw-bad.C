// D4324: a control type whose is_ignored genuinely exists (found via
// ordinary member lookup, right parameter type) but fails to
// constant-fold -- here, by unconditionally throwing -- must be a hard,
// diagnosed compile error, not silently collapse to the same "-1/not
// usable" default that a merely *absent* trait produces.
// contract_control_bool_member used to conflate the two: this compiled
// clean under every -fcontract-evaluation-semantic= before the fix.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }

#include <contracts>

namespace sc = std::contracts;

struct broken_is_ignored {
  static constexpr bool
  is_ignored (sc::assertion_static_info info)
  {
    if (info.semantic () != sc::evaluation_semantic::enforce
	&& info.semantic () != sc::evaluation_semantic::quick_enforce)
      throw "disallowed semantic";
    return false;
  }
  static constexpr bool constify (sc::assertion_static_info) { return true; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }

  void
  operator() (const sc::assertion_context& ctx) const
  {
    if (!ctx.check ())
      __builtin_abort ();
  }
};

inline constexpr broken_is_ignored broken_is_ignored_v{};

// The failed fold is independently reported from more than one internal
// dispatch point (e.g. once while processing the pre<> clause itself,
// again at the function body's own location), so match by message text
// anywhere rather than anchoring to one specific line.
// { dg-error "does not produce a constant expression" "" { target *-*-* } 0 }
// { dg-error "uncaught exception" "" { target *-*-* } 0 }
int f (int x) pre<broken_is_ignored_v>(x >= 0)
{ return x; }
