// Shared by d4324-side-semantic-enforce.C / d4324-side-semantic-observe.C:
// byte-for-byte the same control object and function are compiled in
// both -- only the -fcontract-evaluation-semantic= each test passes
// differs. is_ignored decides activity from *both* the TU's semantic
// and which side (client/definition) a given occurrence is on: under
// enforce the contract is active only on the client (caller-side
// wrapper) side; under observe it's active on both. Neither test uses
// force_client_side_check/force_definition_side_check -- those are
// unconditional, semantic-blind overrides (already covered elsewhere);
// here ordinary policy (-fcontracts-client-check=pre, default
// -fcontracts-definition-check on) puts a copy of the contract on both
// sides, and is_ignored alone decides, per occurrence, whether that
// copy does anything.

#include <contracts>

namespace sc = std::contracts;

int client_calls = 0;
int definition_calls = 0;

struct side_probe {
  static constexpr bool is_ignored (sc::assertion_static_info info)
  {
    // enforce: active on the client side only.
    if (info.semantic () == sc::evaluation_semantic::enforce)
      return info.side () == sc::assertion_check_side::definition;
    // observe: active on both sides.
    if (info.semantic () == sc::evaluation_semantic::observe)
      return false;
    return true;
  }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }

  // Deliberately no termination -- the two counters below are the only
  // test-detectable effect, and they're genuinely different effects
  // (distinct variables for distinct sides), not one counter bumped
  // twice: a bug that fired client-side twice instead of once per side
  // would still leave definition_calls at 0 and be caught.
  void
  operator() (const sc::assertion_context& ctx) const
  {
    if (ctx.static_info ().side () == sc::assertion_check_side::client)
      client_calls++;
    else if (ctx.static_info ().side () == sc::assertion_check_side::definition)
      definition_calls++;
    if (!ctx.check ())
      __builtin_abort ();  // the predicate always holds in these tests
  }
};

inline constexpr side_probe side_probe_v{};

// Split declaration/definition, exactly like d4324-client-check.C: the
// caller-side wrapper is built from the declaration seen at each test's
// own call site, while f's body is defined here, afterward.
int f (int x) pre<side_probe_v>(x >= 0);

int
f (int x)
{
  return x;
}
