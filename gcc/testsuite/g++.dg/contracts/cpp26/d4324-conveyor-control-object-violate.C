// D4324: a control object whose is_conveyor() returns true requires the
// predicate itself (not the control object's own operator()) to satisfy
// the conveyor-function syntactic restrictions.
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

int f (int* p) pre<conveyor_ctrl_v>(reinterpret_cast<long> (p) != 0) // { dg-error "reinterpret_cast. not permitted in a conveyor function or predicate" }
{
  return *p;
}

int main () { int x = 1; return f (&x) - 1; }
