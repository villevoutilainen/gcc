// D4324/P2680: '&arr[literal]' was previously unrecognized by both
// oa_provable_p (Q1, is_object_address) and oa_reference_owned_p (Q2,
// ownership) -- oa_get_range already recognized this exact shape
// (ADDR_EXPR(ARRAY_REF(arr, index))) for tracking a pointer's own base+
// offset fact, but neither of the other two ever gained the equivalent
// case, so 'g (arr[0])' for a conveyor callee taking a non-const 'T&'
// was silently, always rejected regardless of ARR's own provenance.
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

int use_val_mut (int& x) conveyor { return x; }

// A literal, in-bounds index into a local (hence owned) array: both Q1
// (is_object_address) and Q2 (ownership, since the target is a non-const
// reference) must now succeed.
int
accept_array_element () conveyor
{
  int arr[5] = {1, 2, 3, 4, 5};
  return use_val_mut (arr[2]);
}

int
main ()
{
  return accept_array_element () - 3;
}
