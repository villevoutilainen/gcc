// D4324/P2680: -fcontract-conveyor-proofs, predicate-chaining proof,
// OA_UNKNOWN case -- untrusted was never established via a call to a
// function whose postcondition asserts check_it for its own result --
// the connection can't be made, so the best available answer is
// "cannot verify," not silent acceptance, and not a false claim of a
// proven violation either.
//
// The follow-up dg-message demonstrates the diagnostic-precision work
// (oa_unprovable_reason, contracts.h): this is OA_UNPROVABLE_NO_FACT --
// nothing at all established about 'untrusted', as opposed to a fact
// existing for a different predicate/object (OA_UNPROVABLE_WRONG_
// IDENTITY) or under weaker trust than required (OA_UNPROVABLE_WEAKER_
// PROVENANCE) -- see oa_env_predicate_result's own comment for all
// three.
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

// conveyor must be repeated identically on every redeclaration.
bool check_it (int v) conveyor { return v > 0; }

void consume (int x) pre<conveyor_ctrl_v>(check_it (x))
{
  (void) x;
}

void caller (int untrusted)
{
  consume (untrusted); // { dg-warning "cannot verify" }
                       // { dg-message "no fact relating this value" "unprovable reason" { target *-*-* } .-1 }
}

int main () { caller (1); return 0; }
