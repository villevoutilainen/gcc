// D4324/P2680 item 6, Increment I: a callee's own non-ignored,
// conveyor postcondition naming an is_object_address(r) conjunct is a
// trusted fact for the caller's stored return value -- no argument
// substitution is needed (unlike item 7's complementary precondition-
// obligation direction), since a postcondition's guarantee about its
// own return value holds unconditionally for any successful call.
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

int a;

int* g () post<conveyor_ctrl_v>(r: std::is_object_address (r))
{
  return &a;
}

int f ()
{
  int* p = g ();
  contract_assert<conveyor_ctrl_v>(std::is_object_address (p));
  return *p;
}

int main () { a = 5; return f () - 5; }
