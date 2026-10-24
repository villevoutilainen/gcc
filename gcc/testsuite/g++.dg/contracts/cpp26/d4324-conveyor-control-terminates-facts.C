// D4324: a contract's condition is a fact for the analyses only if a
// failed check stops execution.  Whether it does is decided by the
// control object's operator(), found by analyzing its body: it may not
// return normally where assertion_context::check () is false.  The
// library's conveyor_assert and never_proven_conveyor terminate; a
// control object that returns after a violation (observes) doesn't,
// nor does one that terminates on one run-time branch only.  A
// compile-time branch takes the branch this translation unit selects
// (see d4324-conveyor-control-terminates-facts-observe.C).  The
// contract is still an obligation where it's checked.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>
namespace sc = std::contracts;

struct observing_conveyor
{
  static constexpr bool is_ignored (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool constify (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool assumable (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) noexcept { return true; }
  constexpr void operator() (const sc::assertion_context &) const noexcept { }
};

struct observing_never_proven : observing_conveyor
{
  static constexpr bool never_proven (sc::assertion_static_info) noexcept { return true; }
};

/* Terminates unless the semantic is observe: a compile-time choice.  */
struct semantic_conveyor : observing_never_proven
{
  constexpr void operator() (const sc::assertion_context &c) const noexcept
  {
    if (c.check ())
      return;
    if (c.semantic () != sc::evaluation_semantic::observe)
      __builtin_abort ();
  }
};

/* Terminates on one run-time branch only.  */
struct runtime_conveyor : observing_never_proven
{
  constexpr void operator() (const sc::assertion_context &c) const noexcept
  {
    if (c.check ())
      return;
    if (c.comment () && c.comment ()[0] == 'x')
      __builtin_abort ();
  }
};

inline constexpr observing_never_proven observing_v{};
inline constexpr semantic_conveyor semantic_v{};
inline constexpr runtime_conveyor runtime_v{};

int f (int *p) conveyor pre<sc::conveyor_assert_v> (std::is_object_address (p)) { return *p; }
int h (int *p) conveyor pre<sc::never_proven_conveyor_v> (std::is_object_address (p)) { return *p; }
int g (int *p) conveyor pre<observing_v> (std::is_object_address (p))
{ return *p; }		// { dg-error "pointer dereference of .p. not provably valid" }

int a_never_proven (int *q)
{
  contract_assert<sc::never_proven_conveyor_v> (std::is_object_address (q));
  return f (q);
}

int a_observing (int *q)
{
  contract_assert<observing_v> (std::is_object_address (q));
  return f (q);		// { dg-error "cannot prove .is_object_address." }
}

int a_observing_conveyor (int *q) conveyor
{
  contract_assert<observing_v> (std::is_object_address (q));
  return *q;		// { dg-error "pointer dereference of .q. not provably valid" }
}

int a_semantic (int *q)
{
  contract_assert<semantic_v> (std::is_object_address (q));
  return f (q);
}

int a_runtime (int *q)
{
  contract_assert<runtime_v> (std::is_object_address (q));
  return f (q);		// { dg-error "cannot prove .is_object_address." }
}
