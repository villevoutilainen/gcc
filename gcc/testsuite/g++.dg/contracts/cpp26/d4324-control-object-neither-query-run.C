// D4324 (experimental): is_ignored and compute_semantic are mutually
// optional; a control object with neither is never ignored -- its
// assertions are checked even under the ignore semantic.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=ignore" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

int fails;

struct neither_t
{
  constexpr void
  operator() (const assertion_context& c) const
  { if (!c.check ()) ++fails; }
};
inline constexpr neither_t neither{};

int f (int x) pre<neither> (x > 0) { return x; }

int
main ()
{
  f (0);
  if (fails != 1)
    __builtin_abort ();
}
