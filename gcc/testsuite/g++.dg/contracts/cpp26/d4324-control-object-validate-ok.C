// D4324: validate() is a fifth, optional Contract Control Object member
// (not part of P2900/P3400 -- see std/contracts's own P3400 namespace
// comment), entirely separate from is_ignored/constify/assumable: a
// compile-time-only acceptability gate for the control object's own
// configuration, dispatched by the compiler exactly like any other bool
// trait (see contract_control_bool_member/contract_active_p in gcc/cp/
// contracts.cc). A control object that provides validate() and folds it
// to true is accepted, same as one that omits validate() entirely (the
// optional default) -- both forms exercised here to prove neither is
// treated specially over the other.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

struct with_validate {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr bool validate (sc::assertion_static_info) { return true; }
  void
  operator() (const sc::assertion_context& ctx) const
  { if (ctx.check ()) return; __builtin_abort (); }
};
inline constexpr with_validate with_validate_v{};

struct without_validate {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  { if (ctx.check ()) return; __builtin_abort (); }
};
inline constexpr without_validate without_validate_v{};

int f (int x) pre<with_validate_v>(x > 0) { return x; }
int g (int x) pre<without_validate_v>(x > 0) { return x; }

int
main ()
{
  if (f (1) != 1)
    __builtin_abort ();
  if (g (1) != 1)
    __builtin_abort ();
  return 0;
}
