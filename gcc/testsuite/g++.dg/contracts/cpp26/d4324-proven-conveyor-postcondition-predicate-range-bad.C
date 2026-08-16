// D4324: the mirror-image case explicitly requested -- produce() here
// returns a value that check_it's own declared relation proves does
// NOT satisfy check_it, so produce()'s own postcondition claim
// 'check_it(r)' must be caught as *provably false* at produce()'s own
// declaration, not merely "cannot verify" -- exercising the
// OA_PROVEN_FALSE path of the derivation, not just OA_PROVEN_TRUE/
// UNKNOWN (see the companion, provably-true
// d4324-proven-conveyor-postcondition-predicate-range-ok.C). check_it's
// own postcondition uses a plain, non-forced conveyor control object --
// see that companion's own comment for why.
// { dg-do compile { target c++26 } }
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

bool
check_it (int const v) conveyor
  post<conveyor_ctrl_v>(r: r == (v > 0))
{
  return v > 0;
}

int
produce ()
  post<sc::proven_conveyor_v>(r: check_it (r)) // { dg-error "provably false" }
{
  return -1;
}

int main () { return produce (); }
