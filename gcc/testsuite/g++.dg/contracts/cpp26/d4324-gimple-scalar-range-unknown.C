// D4324: GIMPLE mirror of d4324-conveyor-scalar-range-unknown.C --
// diagnostic-precision demo (oa_unprovable_reason, contracts.h) for the
// bare-scalar range obligation on the built-in GIMPLE-pass engine
// (cg_check_call's own range-conjunct loop): 'y' is a plain,
// unconstrained local, so cg_established_range_of finds nothing from
// any of its own several independent sources -- reported as
// OA_UNPROVABLE_UNRESOLVED_OPERAND, since this consult can't
// distinguish which of those sources (if any) came close, unlike AST's
// own NO_FACT/RANGE_PARTIAL split for the identical scenario.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }

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
              // { dg-message "no determinable range" "unprovable reason" { target *-*-* } .-1 }
  return 0;
}
