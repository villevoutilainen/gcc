// D4324/P2680 item 6, Increment I: a callee's compound postcondition
// ('r >= 0 && r < 5') establishes a value range for the caller's
// stored return value, sufficient for the array-bound restriction to
// accept using it as a fixed-size array index. Exercises multi-
// conjunct decomposition (oa_collect_conjuncts) feeding
// oa_call_postcondition_range_p.
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

const int arr[5] = { 10, 11, 12, 13, 14 };

int compute_index (int x) conveyor post<conveyor_ctrl_v>(r: r >= 0 && r < 5)
{
  return x % 5;
}

int f (int x) conveyor
{
  int k = compute_index (x);
  return arr[k];
}

int main () { return f (2) - 12; }
