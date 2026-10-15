// D4324/P2680 Q2 (2026-10-15): the direct forwarding case
// (d4324-reference-ownership-basic.C's own accept_ptr_param_dereference)
// and an arbitrary chain of dereference/address-of starting from the
// same received, proven pointer parameter must now consistently agree
// -- both owned, neither rejected. Confirmed via direct testing this was
// NOT already the case before this fix: binding a local reference from
// a not-yet-owned pointer's pointee, then re-pointer-izing via its own
// address and forwarding THAT pointer's dereference, already compiled
// clean (a pre-existing inconsistency between the direct path and one
// routed through an extra local rebinding); only the direct path was
// rejected. This file pins both forms down together as a parity guard.
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

// Direct: *p forwarded straight to a non-const reference parameter.
int direct (T* p) conveyor
  pre<conveyor_ctrl_v> (std::is_object_address (p))
{
  return use_val_mut (*p);
}

// Laundered through one intermediate local reference binding.
int laundered_via_local_ref (T* p) conveyor
  pre<conveyor_ctrl_v> (std::is_object_address (p))
{
  T& r = *p;
  return use_val_mut (r);
}

// Full round trip: deref into a local reference, address-of that
// reference back into a local pointer, deref again.
int via_deref_then_addr_then_deref (T* p) conveyor
  pre<conveyor_ctrl_v> (std::is_object_address (p))
{
  T& r = *p;
  T* p2 = &r;
  return use_val_mut (*p2);
}

int main ()
{
  T t{1};
  if (direct (&t) != 5) return 1;
  t.v = 1;
  if (laundered_via_local_ref (&t) != 5) return 1;
  t.v = 1;
  if (via_deref_then_addr_then_deref (&t) != 5) return 1;
  return 0;
}
