// D4324 Commit 4: the built-in GIMPLE pass's own mirror of d4324-
// conveyor-call-relational-variable-shift.C -- cg_get_call_relational's
// own fallback to cg_established_range_of (and, through it, GCC's own
// gimple_ranger) for a shift amount that isn't itself a literal.
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

int use_sound_shift (S& v, int i, int k) conveyor
  pre<conveyor_ctrl_v>(i < v.size () && i > 0 && i < 1000000
		       && k <= 0 && k > -1000000)
{
  int j = i + k;
  return v.get (j);
}

int use_unsound_shift (S& v, int i, int k) conveyor
  pre<conveyor_ctrl_v>(i < v.size () && i > 0 && i < 1000000
		       && k >= 0 && k <= 2)
{
  int j = i + k;
  return v.get (j); // { dg-warning "cannot verify that .j. satisfies" }
}

int main ()
{
  S v;
  return use_sound_shift (v, 2, -1) // { dg-warning "cannot verify that .2. satisfies" }
	 + use_unsound_shift (v, 2, 1); // { dg-warning "cannot verify that .2. satisfies" }
}
