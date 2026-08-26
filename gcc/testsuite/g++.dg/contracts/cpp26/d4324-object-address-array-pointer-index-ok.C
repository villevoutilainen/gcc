// D4324/P2680, Increment 1 of the pointer-indexing follow-on:
// '&ptr[dynamic_index]' was previously unrecognized by oa_provable_p
// when PTR is a POINTER_TYPE (as opposed to a directly-named
// ARRAY_TYPE decl, already handled) that itself carries a tracked
// base+offset fact into a named array (oa_get_range) -- mirrors
// oa_scan_array_bounds_in_expr's own existing POINTER_TYPE_P branch,
// asked as an is_object_address question instead of a bounds-safety
// one. Confirmed empirically that a pointer subscript reaches
// oa_provable_p as plain pointer arithmetic ('*(ptr + index*size)'),
// not as ADDR_EXPR(ARRAY_REF(...)) the way a real array's own subscript
// does -- both shapes are exercised here.
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

int use_val_const (const int& x) conveyor { return x; }

// A dynamic, but provably in-bounds, index into a pointer with real
// named-array provenance.
int
accept_pointer_index (int i) conveyor
{
  int arr[4] = {10, 20, 30, 40};
  int* p = &arr[0];
  if (i < 0 || i >= 4)
    return -1;
  return use_val_const (p[i]);
}

int
main ()
{
  return accept_pointer_index (2) - 30;
}
