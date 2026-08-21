// D4324/P2680: companion to the -ok.C case -- one arm unprovable (P's
// pointee, never established) must still reject the whole ternary, not
// silently accept because the OTHER arm happens to be fine.
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
					  // { dg-error "is not owned by the calling function" "ownership" { target *-*-* } .-1 }
}

int main () { return 0; }
