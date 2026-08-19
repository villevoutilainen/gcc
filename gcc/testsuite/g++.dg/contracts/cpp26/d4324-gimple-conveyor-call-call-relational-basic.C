// D4324: the built-in GIMPLE pass's own mirror of d4324-conveyor-call-
// call-relational-basic.C (cg_call_call_rel_fact/cg_get_call_call_
// relational, the call-vs-call analogue of the existing cg_call_rel_
// fact/cg_get_call_relational mechanism) -- "RECEIVER_1.CALLEE_1 () OP
// RECEIVER_2.CALLEE_2 ()" established via a function's own declared
// precondition (self-trust). GIMPLE-side scope is self-trust only,
// never from an ordinary branch (cg_refine_relational_edge_into
// deliberately has no call-vs-call member: such a fact is keyed on an
// object's identity, not an SSA name, so it can't be safely flattened).
// This does NOT match the AST side's own scope, despite an earlier
// revision of this comment claiming it did: oa_refine_single_
// comparison's own oa_match_call_against_call branch DOES establish
// this fact from an ordinary 'if', see d4324-conveyor-relational-
// ifcond.C's own third section, which this GIMPLE mirror has no
// equivalent of at all (found via the sweep documented in
// .claude/plans/lazy-stirring-pearl.md) -- a real, accepted AST/GIMPLE
// asymmetry, not "matching scope."
//
// Every 'S' parameter below is a CONST reference -- see d4324-conveyor-
// relational-ifcond.C's own identical comment for why (P2680 9.1's
// ownership rule, unrelated to what this test is about).
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
};

int use_it (const S& a, const S& b) conveyor pre<conveyor_ctrl_v>(a.size () < b.size ())
{
  return 0;
}

int get_checked (const S& v, const S& w) conveyor pre<conveyor_ctrl_v>(v.size () < w.size ())
{
  return use_it (v, w);
}

int get_unchecked (const S& v, const S& w) conveyor
{
  return use_it (v, w); // { dg-warning "cannot verify that .int S::size\\(\\) const." }
}

int main ()
{
  S v, w;
  return get_checked (v, w) // { dg-warning "cannot verify that .int S::size\\(\\) const." }
	 + get_unchecked (v, w);
}
