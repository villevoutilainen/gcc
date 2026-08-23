// D4324: validate() is a fifth, optional Contract Control Object member
// (not part of P2900/P3400 -- see std/contracts's own P3400 namespace
// comment), entirely separate from is_ignored/constify/assumable: a
// compile-time-only acceptability gate for the control object's own
// configuration, returning std::contracts::contract_validation_result
// (not a plain bool), dispatched by the compiler (see
// contract_control_validates/contract_active_p in gcc/cp/contracts.cc).
// Three shapes, all accepted: a control object whose validate() folds
// to valid == true; one that omits validate() entirely (the optional
// default); and one whose validate() has the wrong return type (a
// plain bool, e.g. code written against an earlier draft of this
// mechanism) -- silently treated as "not a usable validate member",
// exactly like any other absent/mismatched trait, not an error.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

struct with_validate {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  static constexpr sc::contract_validation_result
  validate (sc::assertion_static_info) { return { true }; }
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

struct wrong_validate_type {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  static constexpr bool constify (sc::assertion_static_info) { return false; }
  static constexpr bool assumable (sc::assertion_static_info) { return false; }
  // Wrong return type (should be contract_validation_result) -- must be
  // silently ignored, not dispatched as if it were real.
  static constexpr bool validate (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  { if (ctx.check ()) return; __builtin_abort (); }
};
inline constexpr wrong_validate_type wrong_validate_type_v{};

int f (int x) pre<with_validate_v>(x > 0) { return x; }
int g (int x) pre<without_validate_v>(x > 0) { return x; }
int h (int x) pre<wrong_validate_type_v>(x > 0) { return x; }

int
main ()
{
  if (f (1) != 1)
    __builtin_abort ();
  if (g (1) != 1)
    __builtin_abort ();
  if (h (1) != 1)
    __builtin_abort ();
  return 0;
}
