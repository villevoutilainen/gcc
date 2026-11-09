// { dg-additional-options { -std=c++26 -fmodules-ts -fcontracts -fcontract-control-objects } }

module;
#include <contracts>
export module cco3_two;
// { dg-module-cmi "cco3_two" }

export inline int fails_b;
struct c_t
{
  static constexpr bool
  is_ignored (std::contracts::assertion_static_info) noexcept { return false; }
  constexpr void
  operator() (const std::contracts::assertion_context& c) const
  { if (!c.check ()) ++fails_b; }
};
inline constexpr c_t c{};
export inline double f2 (double a, long b, char d) pre<c> (a > b && d) { return a; }
