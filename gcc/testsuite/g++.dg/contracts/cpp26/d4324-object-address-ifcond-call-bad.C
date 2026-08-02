// D4324/P2680: the IF_STMT/COND_EXPR condition-operand gap -- a call
// used directly inside an if-condition whose argument isn't provable
// at the call site must still be rejected there (item 7), the same as
// if the call had instead flowed through a return or an assignment.
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

bool deref_ok (int* p) pre<conveyor_ctrl_v>(std::is_object_address(p))
{
  return *p == 5;
}

int caller (int* q)
{
  if (deref_ok (q)) // { dg-error "cannot prove .is_object_address. for .q." }
    return 0;
  return 1;
}

int main () { int x = 5; return caller (&x); }
