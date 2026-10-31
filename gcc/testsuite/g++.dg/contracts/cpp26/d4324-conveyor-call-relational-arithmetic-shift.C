// D4324 Commit 2: tracking a call-relational fact through constant-
// offset arithmetic ('j = i +/- k;') -- the motivating scenario: an
// index proven in bounds via 'i < v.size ()', then incremented or
// decremented and re-checked. Both directions exercised: incrementing
// a *tight* bound by 1 correctly reports unverifiable (i could equal
// v.size () - 1, making i + 1 == v.size (), out of bounds), while
// decrementing it correctly verifies (i - 1 < v.size () - 1, which
// still implies i - 1 < v.size ()).
//
// 'i > 0' is also required in both preconditions below: item 8's own
// overflow scan (oa_scan_overflow_in_expr) needs a real basis to prove
// 'i +/- 1' itself can't overflow -- 'i < v.size ()' alone only ever
// establishes a type-bound *upper* witness on i (see oa_type_bound_
// fact), never a numeric bound in either direction, so 'i - 1' has no
// route to provable safety without also excluding i's own, otherwise
// completely open, lower side (i could be INT_MIN, and 'i - 1' would
// genuinely underflow). 'i + 1' is separately rescued via that same
// upper witness once i has one, without needing 'i > 0' at all -- it's
// only 'i - 1' that needs the numeric floor.
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
