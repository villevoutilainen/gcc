// D4324: '<constant> - decl' -- previously explicitly declined by
// oa_get_range's old "decl +/- literal" special case (a literal minus a
// decl negates the whole range rather than shifting it, a shape that
// special case never recognized), now handled for free by the general
// interval-subtraction composition: x in [3,4] -> y = 10 - x in [6,7].
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

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

int
demo_const_minus_decl (int x)
{
  if (x >= 3 && x <= 4)
    {
      int y = 10 - x;
      contract_assert<conveyor_ctrl_v>(y >= 6 && y <= 7);
    }
  return 0;
}

int main () { return demo_const_minus_decl (3); }
