// D4324: diagnostic-precision demo (oa_unprovable_reason, contracts.h)
// for the bare-scalar range obligation (oa_handle_precondition_simple_
// range_obligation): 'y' is a plain, unconstrained local with no
// established fact of any kind, so there is nothing for use_it's own
// precondition obligation to consult -- OA_UNPROVABLE_NO_FACT.
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

void use_it (int x) pre<conveyor_ctrl_v>(x >= 0 && x < 100) {}
extern int opaque ();

int main ()
{
  int y = opaque ();
  use_it (y); // { dg-warning "cannot verify" }
              // { dg-message "no fact relating this value" "unprovable reason" { target *-*-* } .-1 }
  return 0;
}
