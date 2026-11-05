// D4324: a qualified call to a contracted virtual function, d.B::f (), is
// non-virtual ([expr.call]) and runs B::f; only an unqualified call through
// a pointer or reference dispatches to the final overrider (P4445 BUG-03).
// The caller-side check of B::f's precondition runs either way.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int checks;
int ran_b, ran_d;

struct count {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  { ++checks; if (!ctx.check ()) __builtin_abort (); }
};

inline constexpr count count_v{};

struct B
{
  virtual int f (int x) pre<count_v>(x >= 0) { ++ran_b; return 1; }
};

struct D : B
{
  // Extends its base: before the fix, B::f dispatched back to D::f and
  // recursed until the stack overflowed.
  int f (int x) override { ++ran_d; return B::f (x) + 1; }
};

int main ()
{
  D d;
  B& r = d;
  B* p = &d;

  if (d.B::f (1) != 1 || ran_b != 1 || ran_d != 0)
    __builtin_abort ();
  if (p->B::f (1) != 1 || ran_b != 2 || ran_d != 0)
    __builtin_abort ();
  if (r.f (1) != 2 || ran_b != 3 || ran_d != 1)	// dispatches
    __builtin_abort ();
  if (p->f (1) != 2 || ran_b != 4 || ran_d != 2)	// dispatches
    __builtin_abort ();
  if (checks == 0)
    __builtin_abort ();
  return 0;
}
