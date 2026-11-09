// D4324: a prvalue control object is evaluated once, where it is
// written -- with the function its assertion belongs to as the current
// function -- so that std::meta::access_context::current () in its
// constructor names that function, in its definition-side check, in a
// caller-side check at a call site in another function, in a function
// template's instantiation and for a contract_assert; and two
// declarations writing the same control object still match.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -freflection -fcontracts-client-check=pre" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <meta>
using namespace std::contracts;

constexpr bool
same (const char *a, const char *b)
{
  while (*a && *a == *b)
    ++a, ++b;
  return *a == *b;
}

const char *last;
int fails;

struct where_t
{
  const char *fn;
  consteval
  where_t (std::meta::access_context a = std::meta::access_context::current ())
    : fn (std::define_static_string
	    (std::meta::display_string_of (a.scope ())).data ()) {}
  static constexpr bool
  is_ignored (assertion_static_info) noexcept { return false; }
  constexpr void
  operator() (const assertion_context& c) const
  {
    if (!c.check ())
      {
	++fails;
	last = fn;
      }
  }
};

namespace n
{
  int f (int x) pre<where_t{}> (x > 0);
  int f (int x) pre<where_t{}> (x > 0) { return x; }
  void a (int x) { contract_assert<where_t{}> (x > 0); }
  template <class T> T t (T x) pre<where_t{}> (x > 0) { return x; }
}

int
main ()
{
  // Checked on both sides: the caller-side check in main still names f.
  n::f (0);
  if (fails != 2 || !same (last, "int n::f(int)"))
    __builtin_abort ();
  n::a (0);
  if (fails != 3 || !same (last, "void n::a(int)"))
    __builtin_abort ();
  n::t (0);
  if (fails != 5 || !same (last, "T n::t(T) [with T = int]"))
    __builtin_abort ();
}
