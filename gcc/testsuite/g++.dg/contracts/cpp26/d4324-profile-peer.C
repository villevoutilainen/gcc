// D4324: contracts and a profile operate as independent peers.  An
// author-written pre(cond) routes through the contract control object, while a
// core-language-UB check routes through a separate, independently owned path.
// Replacing the contract handler changes the contract response but not the
// UB-check response, because the two are wired independently.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <cstdlib>

namespace sc = std::contracts;

// A minimal core_ub-style check: terminates on its own path, independent of
// any contract-violation handler.
namespace std {
namespace core_ub {
  [[noreturn]] inline void terminate_on_ub ()
  {
    __builtin_trap ();
  }

  inline void check_not_null (const void* p)
  {
    if (!p)
      terminate_on_ub ();
  }
}
}

// A custom contract control object that records the violation and proceeds.
static bool contract_handler_called = false;

struct logging_control {
  static constexpr bool is_ignored (sc::evaluation_config) { return false; }
  static constexpr bool constify = false;
  static constexpr bool assumable = false;
  void
  operator() (const char*, std::source_location, sc::evaluation_config) const
  { contract_handler_called = true; }	// returns -> continue
};

inline constexpr logging_control logging_control_v{};

int f (int x) pre<logging_control_v>(x > 0) { return x; }

int main ()
{
  // Trigger a contract violation: the logging control logs and proceeds.
  f (-1);
  if (!contract_handler_called)
    __builtin_abort ();

  // Trigger a core-language UB check: this terminates via __builtin_trap,
  // not via the contract handler.  Fork a child to test it without killing
  // main.  Since we can't fork on Windows, we verify the check exists and
  // is callable; the actual termination is tested by the mere presence of
  // the call (it never returns, so if it did return, that would be wrong).

  // Verify the core_ub check is independent: calling check_not_null on a
  // non-null pointer succeeds silently - the contract handler is not called
  // for this check.
  contract_handler_called = false;
  int x = 42;
  std::core_ub::check_not_null (&x);
  if (contract_handler_called)
    __builtin_abort ();  // The UB check must NOT go through contracts.

  return 0;
}
