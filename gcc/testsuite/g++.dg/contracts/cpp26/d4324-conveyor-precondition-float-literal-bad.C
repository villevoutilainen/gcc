// D4324: same mechanism as d4324-conveyor-precondition-float-literal-ok.C,
// genuine violation -- an out-of-range literal argument must be caught.
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

void
take_percentage (double p) pre<conveyor_ctrl_v>(p >= 0.0 && p <= 100.0)
{
  (void) p;
}

void
bad_call ()
{
  take_percentage (150.0); // { dg-error "provably violates the precondition" }
}

int main () { return 0; }
