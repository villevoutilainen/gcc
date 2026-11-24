// D4324: a control object whose class has a nontrivial copy constructor.
// Its constant value initializes the object the check calls operator()
// on directly; this used to copy it through a temporary the gimplifier
// rejected (ICE in cp_gimplify_init_expr).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

int failures;

struct C
{
  int v = 0;
  constexpr C () = default;
  constexpr C (int i) : v (i) { }
  constexpr C (const C& o) : v (o.v) { }
  static constexpr bool is_ignored (assertion_static_info) { return false; }
  static constexpr bool constify (assertion_static_info) { return false; }
  static constexpr bool assumable (assertion_static_info) { return false; }
  constexpr void operator() (const assertion_context& c) const
  {
    if (!c.check ())
      {
	if consteval { throw 0; }	// not a constant expression
	else { failures += v; }
      }
  }
};
inline constexpr C c_v {1};

int f (int x) pre<c_v>(x > 0) { return x; }
int g (int x) pre<C (10)>(x > 0) { return x; }
int h (int x) { contract_assert<C (100)>(x > 0); return x; }
constexpr int k (int x) pre<c_v>(x > 0) { return x; }
static_assert (k (1) == 1);

int
main ()
{
  if (f (1) != 1 || g (1) != 1 || h (1) != 1 || failures)
    __builtin_abort ();
  f (0);
  g (0);
  h (0);
  if (failures != 111)
    __builtin_abort ();
}
