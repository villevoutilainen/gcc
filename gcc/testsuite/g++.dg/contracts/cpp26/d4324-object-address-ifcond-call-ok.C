// D4324/P2680: the IF_STMT/COND_EXPR condition-operand gap -- a call
// used directly inside an if-condition (not a bare expression-
// statement, a return value, or an assignment RHS) must still get
// item 7's call-site precondition-obligation check. Here the argument
// is provable at the call site, so it succeeds.
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

bool deref_ok (int* p) pre<conveyor_ctrl_v>(std::is_object_address(p))
{
  return *p == 5;
}

int caller ()
{
  int x = 5;
  if (deref_ok (&x))
    return 0;
  return 1;
}

int main () { return caller (); }
