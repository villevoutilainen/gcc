// D4324/P2680: the paper's own headline example -- a precondition's own
// std::is_object_address(p) is trusted here as an axiom (proof at each
// call site is deferred to item 7, not yet implemented) and the fact is
// seeded into the rest of the function body, so a later contract_assert
// repeating the same check succeeds by relying on that seeded fact
// rather than re-deriving it from scratch.
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
  contract_assert<conveyor_ctrl_v>(std::is_object_address(p));
  return *p;
}

int main () { int x = 5; return deref (&x) - 5; }
