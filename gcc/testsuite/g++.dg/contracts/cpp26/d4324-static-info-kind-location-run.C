// D4324: assertion_static_info::kind() and location() give a control
// object's compile-time queries the assertion's kind and source location,
// as assertion_context::kind() and location() give them at run time.  This
// object ignores every postcondition, every assertion on one line, and
// every assertion in a function whose name mentions "quiet".
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

constexpr bool
contains (const char *s, const char *w)
{
  for (; *s; ++s)
    {
      const char *a = s, *b = w;
      while (*b && *a == *b)
	++a, ++b;
      if (!*b)
	return true;
    }
  return false;
}

constexpr unsigned ignored_line = __LINE__ + 30;

int fails;
struct select_t
{
  static constexpr bool
  is_ignored (assertion_static_info i) noexcept
  {
    return (i.kind () == assertion_kind::post
	    || i.location ().line () == ignored_line
	    || contains (i.location ().function_name (), "quiet"));
  }

  constexpr void
  operator() (const assertion_context& c) const
  {
    if (!c.check ())
      {
	// The static info agrees with the context.
	if (c.static_info ().kind () != c.kind ()
	    || c.static_info ().location ().line () != c.location ().line ())
	  __builtin_abort ();
	++fails;
      }
  }
};
inline constexpr select_t sel{};

int pre_f (int x) pre<sel> (x > 0) { return x; }
int post_f (int x) post<sel> (r: r > 0) { return x; }
int line_f (int x) pre<sel> (x > 0) { return x; }		// ignored_line
int quiet_f (int x) pre<sel> (x > 0) { return x; }
void assert_f (int x) { contract_assert<sel> (x > 0); }

int
main ()
{
  pre_f (0);
  if (fails != 1)
    __builtin_abort ();
  assert_f (0);
  if (fails != 2)
    __builtin_abort ();
  post_f (0);
  line_f (0);
  quiet_f (0);
  if (fails != 2)
    __builtin_abort ();
}
