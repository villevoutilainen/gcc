// D4324/P2680: companion to the -ok.C case -- one arm unprovable (P's
// pointee, never established) must still reject the whole ternary, not
// silently accept because the OTHER arm happens to be fine.
//
// D4324/P2680 (2026-10-15): this used to also expect a separate
// ownership ("is not owned by the calling function") error alongside
// the is_object_address one -- a pointer parameter's pointee is now
// owned once its own is_object_address is proven (see d4324-reference-
// ownership-basic.C's own updated comment), so once Q1 alone correctly
// rejects P's own never-established pointee, Q2 no longer has anything
// independent to add for this specific shape; only the Q1 failure
// remains expected.
// { dg-do compile { target c++26 } }
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

struct T { int v; };

int use_val_mut (T& x) conveyor { return x.v; }

int
reject_ternary_one_arm_unproven (bool c, T* p) conveyor
{
  T a{1};
  return use_val_mut (c ? a : *p); // { dg-error "cannot prove .is_object_address." }
}

int main () { return 0; }
