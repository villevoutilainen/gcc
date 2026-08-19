// Item 8 (overflow) mandatory UB scan, GIMPLE side: the type-bound-
// witness rescue (cg_provably_safe_unit_shift_p/cg_type_bound_fact,
// mirroring contracts.cc's own oa_provably_safe_unit_shift_p/oa_type_
// bound_fact). 'i + 1'/'i - 1' is provably free of overflow purely
// because i has been compared against something of a no-wider integral
// type -- a call's own return type (inc_via_call), a literal
// (dec_via_literal), or another parameter (inc_via_param) -- with no
// numeric bound on i itself ever established. This is exactly the
// shape the general numeric-only route (cg_established_range_of) can't
// prove on its own; d4324-gimple-conveyor-call-relational-arithmetic-
// shift.C is this rescue's own original motivating regression test.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

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

int inc_via_call (S& v, int i) conveyor pre<conveyor_ctrl_v>(i < v.size ())
{
  return i + 1;
}

int dec_via_literal (int i) conveyor pre<conveyor_ctrl_v>(i > 0)
{
  return i - 1;
}

int inc_via_param (int i, int n) conveyor pre<conveyor_ctrl_v>(i < n)
{
  return i + 1;
}

int main ()
{
  S v;
  return inc_via_call (v, 2) // { dg-warning "cannot verify that .2. satisfies" }
	 + dec_via_literal (2) + inc_via_param (2, 10);
}
