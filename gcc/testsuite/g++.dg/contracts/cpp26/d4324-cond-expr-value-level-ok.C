// D4324/P2680: 'cond ? a : b' had no value-level handling anywhere
// (oa_provable_p, oa_provably_nonzero_p, oa_get_range,
// oa_reference_owned_p), despite the branch-merge machinery a ternary
// needs already existing and being well-tested elsewhere (oa_walk_stmt's
// own COND_EXPR case, oa_scan_item8_conjunct). 'int x = c ? 1 : 2;',
// 'p = c ? &a : &b;', and 'f (c ? &a : &b)' all silently gave up
// regardless of whether both arms were individually fine. Each of the
// four functions now recognizes the shape as provable/nonzero/ranged/
// owned iff BOTH arms are.
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

struct T { int v; };

int use_val_mut (T& x) conveyor { return x.v; }
int use_int_mut (int& x) conveyor { return x; }

// oa_provable_p + oa_reference_owned_p: both arms owned and provable.
int
accept_ternary_ref (bool c) conveyor
{
  T a{1}, b{2};
  return use_val_mut (c ? a : b);
}

// oa_provable_p via a local pointer reassigned from a ternary of two
// owned addresses, then dereferenced and forwarded non-const.
int
accept_ternary_pointer_direct (bool c) conveyor
{
  T a{1}, b{2};
  T* p = c ? &a : &b;
  return use_val_mut (*p);
}

// oa_get_range: a ternary selecting between two literal array indices.
int
accept_ternary_range (bool c) conveyor
{
  int arr[5] = {10, 20, 30, 40, 50};
  return use_int_mut (arr[c ? 1 : 2]);
}

int
main ()
{
  int r = accept_ternary_ref (true) - 1;
  r += accept_ternary_pointer_direct (false) - 2;
  r += accept_ternary_range (true) - 20;
  return r;
}
