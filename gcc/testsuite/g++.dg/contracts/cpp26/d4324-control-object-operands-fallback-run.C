// D4324: a control object's operator() is called with the condition's
// operands only if overload resolution for that call finds a viable
// candidate; otherwise it's called with the context alone.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <concepts>

namespace sc = std::contracts;

int plain_calls, operand_calls;

struct int_only_cco
{
  static constexpr bool is_ignored (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool constify (sc::assertion_static_info) noexcept { return false; }
  static constexpr bool assumable (sc::assertion_static_info) noexcept { return false; }
  void operator() (const sc::assertion_context &c) const
  { ++plain_calls; (void) c.check (); }
  template <std::same_as<int>... T>
  void operator() (const sc::assertion_context &c, const T &...) const
  { ++operand_calls; (void) c.check (); }
};

int a (int i, int j) pre<int_only_cco{}> (i < j) { return i; }        // operands
int b (int i, long j) pre<int_only_cco{}> (i < j) { return i; }       // no viable: plain
int c () pre<int_only_cco{}> (true) { return 0; }                     // no operands: plain
int d (int i) { contract_assert<int_only_cco{}> (i > 0); return i; }  // operands

int
main ()
{
  a (1, 2);
  b (1, 2);
  c ();
  d (1);
  if (operand_calls != 2 || plain_calls != 2)
    __builtin_abort ();
}
