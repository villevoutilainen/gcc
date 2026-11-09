// D4324 (experimental): a control object's query may take
// std::contracts::assertion_declaration_info instead of
// assertion_static_info: the static info and a reflection of the function
// the assertion belongs to -- the function a pre/post is declared on (the
// template, for one not yet instantiated; the constructor, not its clones)
// or the one whose body contains a contract_assert.  This one ignores the
// assertions of every function in namespace vendor, nested ones included.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -freflection" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <meta>
using namespace std::contracts;

namespace vendor { }

int fails;
struct by_ns_t
{
  static consteval bool
  is_ignored (assertion_declaration_info d)
  {
    if (d.info.semantic () == evaluation_semantic::ignore)
      return true;
    if (d.function == std::meta::info {})
      return false;
    for (auto p = std::meta::parent_of (d.function); p != ^^::;
	 p = std::meta::parent_of (p))
      if (p == ^^vendor)
	return true;
    return false;
  }
  constexpr void
  operator() (const assertion_context& c) const
  { if (!c.check ()) ++fails; }
};
inline constexpr by_ns_t by_ns{};

namespace vendor
{
  void f (int x) pre<by_ns> (x > 0) { }
  namespace inner { void g (int x) pre<by_ns> (x > 0) { } }
  template <class T> void t (T x) pre<by_ns> (x > 0) { }
}
namespace ours
{
  void h (int x) pre<by_ns> (x > 0) { }
  void a (int x) { contract_assert<by_ns> (x > 0); }
  template <class T> void t (T x) pre<by_ns> (x > 0) { }
  struct S { S (int x) pre<by_ns> (x > 0) { } };
}

int
main ()
{
  vendor::f (0);
  vendor::inner::g (0);
  vendor::t (0);
  if (fails != 0)
    __builtin_abort ();
  ours::h (0);
  ours::a (0);
  ours::t (0);
  ours::S s (0);
  if (fails != 4)
    __builtin_abort ();
}
