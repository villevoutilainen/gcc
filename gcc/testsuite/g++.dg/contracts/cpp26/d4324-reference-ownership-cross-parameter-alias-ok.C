// D4324/P2680 Q2 (2026-10-15): confirms the widened rule (a received
// pointer's pointee is owned once proven) introduces no new soundness
// hazard when the caller aliases two of ITS OWN reference parameters to
// the same object. 'middle(&a)' mutating '*p' through one more
// conveyor-call hop is identical in kind and severity to 'middle'
// mutating '*p' directly ('p->v = 5;'), which was already,
// unconditionally legal with zero Q2 involvement before this change --
// any standing fact 'outer' holds about 'b' is already conservatively
// invalidated by the opaque call to 'middle' regardless, the same
// "any conveyor call invalidates broadly" discipline that already had
// to tolerate the direct-mutation case. Run-tested (not just compiled)
// to confirm the actual, observable aliasing behavior is correct, not
// just that it's accepted.
// { dg-do run { target c++26 } }
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

struct T { int v; };

int use_val_mut (T& x) conveyor { x.v = 5; return x.v; }

int middle (T* p) conveyor
  pre<conveyor_ctrl_v> (std::is_object_address (p))
{
  return use_val_mut (*p);
}

int outer (T& a, T& b) conveyor
{
  middle (&a);
  return b.v;
}

int main ()
{
  // a and b alias the SAME object: middle's mutation through a is
  // observable through b too.
  T x{1};
  if (outer (x, x) != 5) return 1;
  // a and b are genuinely distinct objects: middle's mutation through a
  // leaves b untouched.
  T y{1}, z{2};
  if (outer (y, z) != 2) return 1;
  return 0;
}
