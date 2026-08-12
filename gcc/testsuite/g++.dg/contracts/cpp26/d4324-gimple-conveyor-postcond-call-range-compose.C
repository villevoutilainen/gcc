// D4324: the built-in GIMPLE pass's own mirror of d4324-conveyor-
// postcond-call-range-compose.C -- genuinely new plumbing bridging two
// previously-independent analyses (the simple, self-trust-only linear
// pass established_range/cg_established_range_of lives in, and the
// dominator-tree fixed-point walk field/call-range facts live in, for
// branch-sensitive tracking) via a shared SCALAR_RANGE_CACHE, computed
// by the dominator pass (which now runs first) and consulted by the
// simple pass afterward. Unlike the AST side, the GIMPLE side composes
// *before* this same call's own argument invalidation as a matter of
// this walk's own natural statement order (cg_compose_call_result_
// range runs right after cg_consult_persistent_facts, before cg_
// invalidate_persistent_facts_for_call_args) -- so, unlike contracts.cc,
// no separate "eager" special case was needed here to handle the self-
// referential case correctly.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }

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
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

struct S {
  int size () const conveyor { return 5; }
  int make () const conveyor post<conveyor_ctrl_v>(r: r < size ()) { return 1; }
};

int consume (int n) conveyor pre<conveyor_ctrl_v>(n < 10) { return n; }

int use_it (S& s) conveyor
{
  if (s.size () < 10)
    {
      int y = s.make ();
      return consume (y);
    }
  return -1;
}

int use_it_unchecked (S& s) conveyor
{
  int y = s.make ();
  return consume (y); // { dg-warning "cannot verify that .y. satisfies" }
}

int main () { S s; return use_it (s) + use_it_unchecked (s); }
