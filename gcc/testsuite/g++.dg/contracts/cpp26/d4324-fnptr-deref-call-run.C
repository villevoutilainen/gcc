// D4324: the contract on a function pointer governs a call through it
// however the call is spelled: p(x), (*p)(x), (**p)(x), (c ? p : p)(x) and
// (*s.fp)(x) all call through the contracted object itself (P4445 BUG-07).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int violations;

struct count {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) ++violations; }	// observes: returns and continues
};

inline constexpr count count_v{};

void f (int) {}

void (*p) (int a) pre<count_v>(a > 0) = f;

struct S
{
  void (*fp) (int a) pre<count_v>(a > 0) = f;
};

int main (int argc, char **)
{
  bool c = argc > 0;
  S s;
  p (0);
  (*p) (0);
  (**p) (0);
  (c ? p : p) (0);
  (*s.fp) (0);
  (*p) (1);			// holds
  return violations == 5 ? 0 : 1;
}
