// D4324: the cross-call follow-up to d4324-conveyor-conversion-
// relational-ok.C -- forwarding two class-typed decls BY VALUE to a
// *different* function needing the same relational fact, for a non-
// trivially-copyable type. g's own precondition "x < q" self-trust-
// seeds a relational fact between its own x/q; each is materialized as
// a real copy-constructor call at the 'f (x, q)' call site (the same
// AGGR_INIT_EXPR shape as d4324-conveyor-conversion-cross-call-simple-
// ok.C), resolved by the same oa_strip_conversion_call fix, so the
// obligation is discharged purely by matching identity, never
// resolving either wrap's value. See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct conveyor_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr conveyor_ctrl ctrl_v{};

struct wrap {
  int v;
  wrap (int v_) : v (v_) {}
  wrap (const wrap &other) : v (other.v) {}
  ~wrap () {}
  operator int () const conveyor { return v; }
};

int f (wrap x, wrap q) pre<ctrl_v> (x < q) { return x; }

int g (wrap x, wrap q) pre<ctrl_v> (x < q)
{
  return f (x, q);
}

int main () { return g (wrap (2), wrap (5)) - 2; } // { dg-warning "cannot verify" }
