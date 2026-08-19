// D4324/P2680: companion to the -ok.C case -- a variable, untracked index
// still correctly fails both is_object_address (since the new ARRAY_REF
// recognition in oa_provable_p requires the index to be provably within
// [0, N), not merely that ARR itself is a directly-named array) and the
// pre-existing, independent item-8 array-bounds scan.
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

int use_val_mut (int& x) conveyor { return x; }

int
reject_untracked_index (int idx) conveyor
{
  int arr[5] = {1, 2, 3, 4, 5};
  return use_val_mut (arr[idx]); // { dg-error "cannot prove .is_object_address. for .arr\\\[idx\\\]." }
                                  // { dg-error "array index .idx. not provably in-bounds" "" { target *-*-* } .-1 }
}

int main () { return 0; }
