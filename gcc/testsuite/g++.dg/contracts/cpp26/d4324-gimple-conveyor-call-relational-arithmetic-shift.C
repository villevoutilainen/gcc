// D4324 Commit 2: the built-in GIMPLE pass's own mirror of d4324-
// conveyor-call-relational-arithmetic-shift.C -- cg_get_call_
// relational's own PLUS_EXPR/MINUS_EXPR-by-constant transfer, and
// cg_offset_compatible_with_code's own sign check at consult time.
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
  int get (int n) const conveyor pre<conveyor_ctrl_v>(n < size ()) { return n; }
};

int use_unsound (S& v, int i) conveyor pre<conveyor_ctrl_v>(i < v.size () && i > 0)
{
  int j = i + 1;
  return v.get (j); // { dg-warning "cannot verify that .j. satisfies" }
}

int use_sound (S& v, int i) conveyor pre<conveyor_ctrl_v>(i < v.size () && i > 0)
{
  int j = i - 1;
  return v.get (j);
}

int main ()
{
  S v;
  return use_unsound (v, 2) // { dg-warning "cannot verify that .2. satisfies" }
	 + use_sound (v, 2); // { dg-warning "cannot verify that .2. satisfies" }
}
