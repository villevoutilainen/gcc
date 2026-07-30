// D4324: std::contracts::noexcept_assert is always active -- its
// is_ignored unconditionally returns false, regardless of
// -fcontract-evaluation-semantic=. Under plain -fcontract-evaluation-
// semantic=ignore (which would make every *other* control type here
// silently skip the check), noexcept_assert_v still genuinely enforces
// and terminates, reconstructing __glibcxx_assert_fail's own diagnostic
// (file/line/function from the real call site, not this header's own,
// plus the predicate's own auto-derived condition text, since no
// per-instance message was given) via the plain fallback path (neither
// _GLIBCXX_ASSERTIONS nor _GLIBCXX_ASSERTIONS_USE_VIOLATION_HANDLER is
// defined here).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=ignore" }
// { dg-shouldfail "noexcept_assert always enforces, even under -fcontract-evaluation-semantic=ignore" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int f (int x)
{
  contract_assert<sc::noexcept_assert_v>(x >= 0);
  return x;
}

int main () { return f (-1); }

// { dg-output "Assertion 'x >= 0' failed" }
