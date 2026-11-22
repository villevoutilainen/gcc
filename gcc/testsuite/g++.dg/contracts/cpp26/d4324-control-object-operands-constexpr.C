// D4324: a control object's operator() gets the condition's operands in
// constant evaluation too.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

struct cco
{
  static constexpr bool is_ignored (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool constify (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool assumable (sc::assertion_static_info) noexcept { return false; }
  constexpr void operator() (const sc::assertion_context &c) const
  { if (!c.check ()) throw 1; }
  template <class... T>
  constexpr void operator() (const sc::assertion_context &c, const T &...ops) const
  { if (!c.check () && ((ops > 100) || ...)) throw 2; }
};

constexpr int f (int i, int x) pre<cco{}> (i < x) { return i; }
static_assert (f (1, 2) == 1);
static_assert (f (5, 2) == 5);	// violated, but no operand above 100
constexpr int g = f (500, 2);	// { dg-error "uncaught exception" }
