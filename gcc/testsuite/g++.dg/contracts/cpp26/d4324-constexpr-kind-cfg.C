// D4324: assertion_context::kind()/semantic() report the right
// std::contracts::assertion_kind/evaluation_semantic during constant
// evaluation, exactly as they do at runtime (see d4324-kind-mapping-run.C /
// d4324-cfg-mapping-run.C).  A control object can't return a value from
// operator() (it's void), so values are observed here via the same
// C++26 constexpr throw/catch fidelity d4324-constexpr-throw-fidelity.C
// establishes: throw the observed value out to a small wrapper that
// catches it and returns it, checkable via static_assert.
//
// Postconditions are deliberately not exercised here: naming a control
// object on a postcondition inside a constexpr function hits a pre-existing
// limitation unrelated to control objects at all -- the postcondition
// result variable is not yet usable in a constant expression even on the
// bare P2900 (-fcontract-control-objects off) path (confirmed separately;
// not this feature's regression to fix).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }

#include <contracts>

namespace sc = std::contracts;

struct probe_kind {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify (sc::evaluation_semantic) { return false; }
  static constexpr bool assumable (sc::evaluation_semantic) { return false; }

  constexpr void
  operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.check ())
      return;
    throw ctx.kind ();
  }
};

inline constexpr probe_kind probe_kind_v{};

struct probe_semantic {
  static constexpr bool is_ignored (sc::evaluation_semantic) { return false; }
  static constexpr bool constify (sc::evaluation_semantic) { return false; }
  static constexpr bool assumable (sc::evaluation_semantic) { return false; }

  constexpr void
  operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.check ())
      return;
    throw ctx.semantic ();
  }
};

inline constexpr probe_semantic probe_semantic_v{};

constexpr int f (int x) pre<probe_kind_v>(x >= 0) { return x; }
constexpr int fc (int x) pre<probe_semantic_v>(x >= 0) { return x; }

constexpr void
h (int x)
{
  contract_assert<probe_kind_v>(x >= 0);
}

constexpr sc::assertion_kind
observe_pre_kind ()
{
  try { f (-1); } catch (sc::assertion_kind k) { return k; }
  return sc::assertion_kind::assert; // unreachable
}

constexpr sc::assertion_kind
observe_assert_kind ()
{
  try { h (-1); } catch (sc::assertion_kind k) { return k; }
  return sc::assertion_kind::pre; // unreachable
}

constexpr sc::evaluation_semantic
observe_semantic ()
{
  try { fc (-1); } catch (sc::evaluation_semantic c) { return c; }
  return sc::evaluation_semantic::ignore; // unreachable
}

static_assert (observe_pre_kind () == sc::assertion_kind::pre);
static_assert (observe_assert_kind () == sc::assertion_kind::assert);
static_assert (observe_semantic () == sc::evaluation_semantic::observe);
