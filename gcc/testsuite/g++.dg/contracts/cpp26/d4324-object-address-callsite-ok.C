// D4324/P2680: the call-site precondition-obligation mechanism (item 7)
// -- the paper's own deref(int* p) pre<...>(is_object_address(p)) example,
// called from another function. The obligation is discharged at the
// call site, using the caller's own (provable) argument expression,
// independent of whatever oa_handle_precondition_stmt trusted inside
// deref's own body.
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

int deref (int* p) pre<conveyor_ctrl_v>(std::is_object_address(p))
{
  return *p;
}

int caller ()
{
  int x = 5;
  return deref (&x);
}

int main () { return caller () - 5; }
