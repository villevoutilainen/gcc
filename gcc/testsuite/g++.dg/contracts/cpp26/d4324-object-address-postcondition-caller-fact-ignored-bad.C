// D4324/P2680 item 6, Increment I: the well-formedness gate
// (is_conveyor && !is_ignored) must be explicitly re-checked at this
// new fact-sourcing site too, not assumed from the callee having
// compiled -- here the callee's postcondition control object reports
// is_ignored() == true, so the is_object_address fact must NOT
// transfer to the caller's stored result.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct ignoring_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return true; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr ignoring_ctrl ignoring_ctrl_v{};

struct conveyor_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr conveyor_ctrl conveyor_ctrl_v{};

int a;

int* g () post<ignoring_ctrl_v>(r: std::is_object_address (r))
{
  return &a;
}

int f ()
{
  int* p = g ();
  contract_assert<conveyor_ctrl_v>(std::is_object_address (p)); // { dg-error "cannot prove" }
  return *p;
}

int main () { a = 5; return f () - 5; }
