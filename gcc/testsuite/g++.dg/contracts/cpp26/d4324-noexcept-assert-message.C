// D4324: noexcept_assert("some message") -- a fresh temporary, one per
// distinct message, aggregate-paren-initialized exactly like
// d4324-control-object-state.C's labeled("temp diagnostic") -- routes
// its per-instance message to the real, replaceable
// ::handle_contract_violation (via invoke_violation_handler) in place of
// the predicate's own auto-derived condition text, when both
// _GLIBCXX_ASSERTIONS and _GLIBCXX_ASSERTIONS_USE_VIOLATION_HANDLER are
// defined. The reported semantic is always enforce, regardless of this
// TU's own -fcontract-evaluation-semantic=observe -- noexcept_assert has
// no other semantic to report, per its own always-enforcing design.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#define _GLIBCXX_ASSERTIONS
#define _GLIBCXX_ASSERTIONS_USE_VIOLATION_HANDLER
#include <contracts>
#include <cstdlib>

namespace sc = std::contracts;

void
handle_contract_violation (const sc::contract_violation& v)
{
  bool ok = v.semantic () == sc::evaluation_semantic::enforce
	    && __builtin_strcmp (v.comment (), "custom message") == 0;
  std::exit (ok ? 0 : 1);
}

int f (int x)
{
  contract_assert<sc::noexcept_assert("custom message")>(x >= 0);
  return x;
}

int main () { return f (-1); }
