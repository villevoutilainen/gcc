// D4324: as d4324-conveyor-control-terminates-facts.C, with the
// observe semantic: a control object whose operator() terminates
// unless the semantic is observe now returns, so its contract
// establishes nothing.  The library's conveyor control objects ignore
// the semantic and still terminate.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }

#include <contracts>
namespace sc = std::contracts;

struct semantic_conveyor
{
  static constexpr bool is_ignored (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool constify (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool assumable (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool is_conveyor (sc::assertion_static_info) noexcept { return true; }
  static constexpr bool never_proven (sc::assertion_static_info) noexcept { return true; }
  constexpr void operator() (const sc::assertion_context &c) const noexcept
  {
    if (c.check ())
      return;
    if (c.semantic () != sc::evaluation_semantic::observe)
      __builtin_abort ();
  }
};
inline constexpr semantic_conveyor semantic_v{};

int f (int *p) conveyor pre<sc::conveyor_assert_v> (std::is_object_address (p)) { return *p; }

int a_never_proven (int *q)
{
  contract_assert<sc::never_proven_conveyor_v> (std::is_object_address (q));
  return f (q);
}

int a_semantic (int *q)
{
  contract_assert<semantic_v> (std::is_object_address (q));
  return f (q);		// { dg-error "cannot prove .is_object_address." }
}
