// { dg-module-do run }
// { dg-additional-options { -std=c++26 -fmodules-ts -fcontracts -fcontract-control-objects } }
// D4324: contract checks capture their operands in a class of their own
// with a TU-local name; two modules' checks of different shapes, and the
// importer's own (including caller-side checks of the imported
// functions), coexist without being mistaken for one another.

module;
#include <contracts>
export module cco3_one;
// { dg-module-cmi "cco3_one" }

export inline int fails_a;
struct c_t
{
  static constexpr bool
  is_ignored (std::contracts::assertion_static_info) noexcept { return false; }
  constexpr void
  operator() (const std::contracts::assertion_context& c) const
  { if (!c.check ()) ++fails_a; }
};
inline constexpr c_t c{};
export inline int f1 (int x) pre<c> (x > 0) { return x; }
