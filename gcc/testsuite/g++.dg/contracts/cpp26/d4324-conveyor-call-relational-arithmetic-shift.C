// D4324 Commit 2: tracking a call-relational fact through constant-
// offset arithmetic ('j = i +/- k;') -- the motivating scenario: an
// index proven in bounds via 'i < v.size ()', then incremented or
// decremented and re-checked. Both directions exercised: incrementing
// a *tight* bound by 1 correctly reports unverifiable (i could equal
// v.size () - 1, making i + 1 == v.size (), out of bounds), while
// decrementing it correctly verifies (i - 1 < v.size () - 1, which
// still implies i - 1 < v.size ()).
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

int use_unsound (S& v, int i) conveyor pre<conveyor_ctrl_v>(i < v.size ())
{
  int j = i + 1;
  return v.get (j); // { dg-warning "cannot verify that .j. satisfies" }
}

int use_sound (S& v, int i) conveyor pre<conveyor_ctrl_v>(i < v.size ())
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
