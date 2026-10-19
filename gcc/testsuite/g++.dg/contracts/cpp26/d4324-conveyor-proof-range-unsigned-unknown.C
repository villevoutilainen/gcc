// D4324: unsigned arithmetic wraps on overflow (well-defined modular
// behavior), not UB -- so composing it exactly is sound only when no
// value in the range can wrap: both bounds known and representable
// ('x in [3, 4]' makes 'x + 5u' exactly [8, 9]).  Otherwise composition
// declines ("cannot verify"), never assuming wraparound-free arithmetic
// -- and never calling the condition false: 'x >= 3u' makes 'x + 5u'
// wrap for the largest x.
// { dg-do compile { target c++26 } }
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
demo_unsigned (unsigned x)
{
  if (x >= 3u && x <= 4u)
    {
      unsigned y = x + 5u;
      contract_assert<conveyor_ctrl_v>(y >= 8u && y <= 9u);
    }
  return 0;
}

int
demo_unsigned_may_wrap (unsigned x)
{
  if (x >= 3u)
    {
      unsigned y = x + 5u;
      contract_assert<conveyor_ctrl_v>(y >= 8u); // { dg-warning "cannot verify" }
    }
  return 0;
}

int main () { return 0; }
