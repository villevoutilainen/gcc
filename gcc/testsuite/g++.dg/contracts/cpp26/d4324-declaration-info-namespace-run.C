// D4324 (experimental): assertion_declaration_info::enclosing_namespace is
// the innermost namespace enclosing the assertion, classes skipped -- that
// of its function, a lambda's included -- so a control object can match
// a namespace exactly without walking parent_of.  This one ignores the
// assertions directly in namespace vendor (and in a function: the
// lambda's contract_assert has its operator()).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -freflection" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

namespace vendor { }

int fails;
struct in_vendor_t
{
  static consteval bool
  is_ignored (assertion_declaration_info d)
  {
    return (d.enclosing_namespace == ^^vendor
	    && d.function != decltype (^^int) {});
  }
  constexpr void
  operator() (const assertion_context& c) const
  { if (!c.check ()) ++fails; }
};
inline constexpr in_vendor_t in_vendor{};

namespace vendor
{
  void f (int x) pre<in_vendor> (x > 0) { }
  struct S { void m (int x) pre<in_vendor> (x > 0) { } };
  inline auto lam = [] (int x) { contract_assert<in_vendor> (x > 0); };
  namespace inner { void g (int x) pre<in_vendor> (x > 0) { } }
}
void global (int x) pre<in_vendor> (x > 0) { }

int
main ()
{
  vendor::f (0);
  vendor::S {}.m (0);
  vendor::lam (0);
  if (fails != 0)
    __builtin_abort ();
  // Not directly in vendor.
  vendor::inner::g (0);
  global (0);
  if (fails != 2)
    __builtin_abort ();
}
