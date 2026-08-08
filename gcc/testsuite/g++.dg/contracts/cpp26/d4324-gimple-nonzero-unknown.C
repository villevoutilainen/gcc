// The built-in GIMPLE-pass engine's own OA_UNKNOWN case for
// nonzero-ness (-fcontract-conveyor-proofs-gimple): 'm' is a plain,
// unconstrained integer parameter with no established fact of any
// kind, so there is nothing for consumer's own precondition obligation
// to consult. Unlike is_object_address, there is no mandatory
// call-site obligation check for nonzero-ness at all, so this compiles
// and runs successfully with only this engine's own warning -- the
// sound answer is "cannot verify," not silent acceptance. See
// gcc/cp/contracts-gimple.cc and ~/gimple-contract-analysis.md.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

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

int consumer (int n) pre<conveyor_ctrl_v>(n != 0) { return 10 / n; }

int relay (int m)
{
  return consumer (m); // { dg-warning "cannot verify that .m. is nonzero" }
}

int main () { return relay (5) - 2; }
