// D4324: the built-in GIMPLE pass's own mirror of d4324-conveyor-call-
// range-basic.C/d4324-conveyor-field-range-ifcond.C -- both the call-
// range shape itself and its local if-condition establishment
// (cg_refine_edge_into, genuinely new infrastructure: unlike a bare
// SSA range, already handled for free by GCC's own gimple_ranger,
// field/call-range facts had no branch-condition refinement at all
// before this). Exercises both field and call receivers, and the
// 'tmp = a > b; if (tmp != 0)' lowering GIMPLE_COND actually produces
// (confirmed via direct testing that a naive direct-embedded-
// comparison assumption never fired at all).
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
  int count;
  int size () const conveyor { return 5; }
  int get_field () const conveyor pre<conveyor_ctrl_v>(count > 3) { return 1; }
  int get_call () const conveyor pre<conveyor_ctrl_v>(size () > 3) { return 1; }
};

int checked_field (S& s) conveyor
{
  if (s.count > 3)
    return s.get_field ();
  return -1;
}

int checked_call (S& s) conveyor
{
  if (s.size () > 3)
    return s.get_call ();
  return -1;
}

int unchecked_field (S& s) conveyor
{
  return s.get_field (); // { dg-warning "cannot verify that field .S::count." }
}

int unchecked_call (S& s) conveyor
{
  return s.get_call (); // { dg-warning "cannot verify that .int S::size\\(\\) const." }
}

int main ()
{
  S s;
  s.count = 5;
  return checked_field (s) + checked_call (s)
	 + unchecked_field (s) + unchecked_call (s);
}
