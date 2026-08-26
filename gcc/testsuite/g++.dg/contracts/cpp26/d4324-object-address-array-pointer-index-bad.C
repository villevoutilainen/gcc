// D4324/P2680, companion to the -ok.C case: a variable, unbounded index
// into a pointer with real named-array provenance still correctly fails
// both is_object_address (Increment 1's own new POINTER_TYPE_P branch
// requires the composed offset to be provably within [0, N), not merely
// that the pointer carries a tracked base at all) and the pre-existing,
// independent item-8 array-bounds scan.
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

int use_val_const (const int& x) conveyor { return x; }

int
reject_unbounded_pointer_index (int i) conveyor
{
  int arr[4] = {10, 20, 30, 40};
  int* p = &arr[0];
  return use_val_const (p[i]); // { dg-error "cannot prove .is_object_address." }
                                // { dg-error "pointer arithmetic not provably in-bounds" "" { target *-*-* } .-1 }
}

int main () { return 0; }
