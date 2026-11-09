// D4324: a prvalue control object of a class with a (constexpr or
// consteval) constructor, in a function template's precondition, is
// folded in its template form, not given to the constant evaluator
// as one (an ICE).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

int fails;

template <bool Consteval>
struct count_t
{
  int weight;
  constexpr count_t (int w = 1) requires (!Consteval) : weight (w) {}
  consteval count_t (int w = 1) requires Consteval : weight (w) {}
  static constexpr bool
  is_ignored (assertion_static_info) noexcept { return false; }
  constexpr void
  operator() (const assertion_context& c) const
  { if (!c.check ()) fails += weight; }
};

template <class T> void f (T x) pre<count_t<false>{}> (x > 0) {}
template <class T> void g (T x) pre<count_t<true>{2}> (x > 0) {}
template <class T> void h (T x) pre<count_t<false>{T (4)}> (x > 0) {}

int
main ()
{
  f (0);
  g (0);
  h (0);
  if (fails != 7)
    __builtin_abort ();
}
