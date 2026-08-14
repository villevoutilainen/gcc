// D4324/P2680 item 8's overflow scan: the general numeric route's own
// CALL_EXPR-range resolution (item 6) -- 'x = x + v.size ()' is
// provable via the general PLUS_EXPR path (not the type-bound-witness
// rescue, which only ever fires for a literal shift of exactly 1)
// because size () itself carries a bounding postcondition, giving the
// call a real, two-sided numeric range to add against x's own.
//
// Deliberately spelled as 'x = x + v.size ()' rather than the
// equivalent 'x +=  v.size ()': found via direct testing that the
// compound-assignment operator's own desugaring routes the call
// through a TARGET_EXPR-managed temporary (the PLUS_EXPR ends up
// referencing that temporary's own bare, compiler-generated slot decl,
// not the CALL_EXPR itself), which oa_get_range has no route to
// resolve back to the call that initializes it -- a disclosed,
// separate limitation from the one this test is actually about, not
// fixed here.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

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
  int size () const conveyor post<conveyor_ctrl_v>(r: r >= 0 && r < 100)
  { return 5; }
};

int use_call_bound_addition (S& v, int x) conveyor
  pre<conveyor_ctrl_v>(x > -100 && x < 100)
{
  x = x + v.size ();
  return x;
}

int main ()
{
  S v;
  return use_call_bound_addition (v, 3) - 8;
}
