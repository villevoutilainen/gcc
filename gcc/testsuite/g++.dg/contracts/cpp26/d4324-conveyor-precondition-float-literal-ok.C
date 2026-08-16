// D4324: a precondition's own combined-range obligation, discharged
// against a literal REAL_CST call-site argument -- the floating-point
// analogue of oa_handle_call_conveyor_proof_obligation's existing
// integer combined-range check (oa_match_simple_comparison/
// oa_tighten_range_bound/oa_env_check_range_subsumption), now also
// populated for REAL_CST via oa_float_tighten_range_bound/
// oa_env_check_float_range_subsumption.
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
good_call ()
{
  take_percentage (50.0);
}

int main () { good_call (); return 0; }
