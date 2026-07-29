// D4324: a P4324 (control-object) contract is now legal on a virtual
// function -- relaxing [dcl.contract.func]p6 for control-object
// contracts only (bare/P2900 contracts on virtual functions stay
// prohibited, see dcl.contract.func.p4.C/p6.C, which don't pass
// -fcontract-control-objects and so are unaffected by this).
//
// Derived deliberately does not override f: a call through a Base*
// pointing at a Derived instance is still a genuinely virtual call
// (dispatched through the vtable), yet still lands on Base::f's own
// body, exercising Base::f's own definition-side contract.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

namespace sc = std::contracts;

int calls = 0;

struct probe {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  {
    calls++;
    if (!ctx.check ())
      __builtin_abort ();
  }
};

inline constexpr probe probe_v{};

struct Base {
  virtual int f (int x) pre<probe_v>(x >= 0) { return x; }
  virtual ~Base () {}
};

struct Derived : Base {
  int g () { return 99; }
};

int main ()
{
  Base* b = new Derived ();
  if (b->f (5) != 5)
    __builtin_abort ();
  if (calls != 1)
    __builtin_abort ();
  delete b;
  return 0;
}
