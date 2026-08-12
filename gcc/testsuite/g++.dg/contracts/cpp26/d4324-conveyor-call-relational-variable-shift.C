// D4324 Commit 4: tracking a call-relational fact through a *variable*
// (not literal) offset -- 'j = i + k;' where k's own range comes from a
// second tracked parameter, not a compile-time constant. Both
// directions exercised: shifting by a range whose own worst case (upper
// bound) is still <= 0 correctly verifies regardless of k's own lower
// bound (or lack of one), while shifting by a range whose own worst
// case could be positive correctly reports unverifiable.
// { dg-do compile { target c++26 } }
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
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

struct S {
  int size () const conveyor { return 5; }
  int get (int n) const conveyor pre<conveyor_ctrl_v>(n < size ()) { return n; }
};

int use_sound_shift (S& v, int i, int k) conveyor
  pre<conveyor_ctrl_v>(i < v.size () && k <= 0)
{
  int j = i + k;
  return v.get (j);
}

int use_unsound_shift (S& v, int i, int k) conveyor
  pre<conveyor_ctrl_v>(i < v.size () && k >= 0 && k <= 2)
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
