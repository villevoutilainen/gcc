// D4324: the same predicate that would violate conveyor rules is fine
// under a control object whose is_conveyor() returns false (or is absent).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct plain_ctrl {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) { return false; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr plain_ctrl plain_ctrl_v{};

int f (int* p) pre<plain_ctrl_v>(reinterpret_cast<long> (p) != 0)
{
  return *p;
}

int main () { int x = 1; return f (&x) - 1; }
