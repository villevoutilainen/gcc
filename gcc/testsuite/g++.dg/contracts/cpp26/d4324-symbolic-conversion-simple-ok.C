// D4324: -fcontract-symbolic-proofs' own axis of the same conversion-
// lookthrough covered for -fcontract-conveyor-proofs by
// d4324-conveyor-conversion-simple-ok.C -- oa_handle_call_symbolic_
// precondition_obligation shares oa_match_simple_comparison/oa_get_
// range/oa_env_check_comparison with the conveyor path, so a class-
// typed parameter reaching a scalar comparison via its own conversion
// operator is recognized here too, entirely at compile time. See
// .claude/plans/well-we-last-discussed-ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs" }

#include <contracts>
namespace sc = std::contracts;

struct symbolic_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl symbolic_ctrl_v{};

struct wrap {
  int v;
  constexpr wrap (int v_) : v (v_) {}
  constexpr operator int () const { return v; }
};

int need_small (int x) pre<symbolic_ctrl_v> (x < 10) { return x; }

int f (wrap q) pre<symbolic_ctrl_v> (q < 5)
{
  return need_small (q);
}

int main ()
{
  return f (wrap (2));
}
