// P4340: a defaulted reflect_constant is a defaulted member function,
// defined as deleted only where its class can't be structural; reflection
// tells which.  It has no definition: an odr-use of one that isn't deleted
// is ill-formed (P4340R1: std::meta::reflect_constant does the same thing).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }

#include <meta>

class P {
  int v;
public:
  constexpr P (int i) : v (i) {}
  constexpr int get () const { return v; }
  consteval std::meta::info reflect_constant () const = default; // { dg-message "only makes its class structural" }
};
template<class T> struct S {
  T t;
  consteval std::meta::info reflect_constant () const = default; // { dg-message "defined as deleted because .S<Q>. is not a structural type" }
};
class Q { int v; };
struct D {
  int i;
  consteval std::meta::info reflect_constant () const = delete;
};

consteval std::meta::info
find_rc (std::meta::info c)
{
  for (auto m : members_of (c, std::meta::access_context::unchecked ()))
    if (is_function (m) && has_identifier (m)
	&& identifier_of (m) == "reflect_constant")
      return m;
  return std::meta::info{};
}

static_assert (is_defaulted (find_rc (^^P)));
static_assert (!is_deleted (find_rc (^^P)));
static_assert (!is_user_provided (find_rc (^^P)));
static_assert (is_defaulted (find_rc (^^S<int>)));
static_assert (!is_deleted (find_rc (^^S<int>)));
static_assert (is_defaulted (find_rc (^^S<Q>)));
static_assert (is_deleted (find_rc (^^S<Q>)));
static_assert (!is_defaulted (find_rc (^^D)));
static_assert (is_deleted (find_rc (^^D)));

template<P V> constexpr int pget = V.get ();
constexpr P p{42};
static_assert (pget<p> == 42);
static_assert ([: std::meta::reflect_constant (p) :].get () == 42);
using T = decltype (p.reflect_constant ());	// not an odr-use

consteval void
g ()
{
  (void) p.reflect_constant ();		// { dg-error "which has no definition" }
  auto m = &P::reflect_constant;	// { dg-error "which has no definition" }
  (void) p.[: find_rc (^^P) :] ();	// { dg-error "which has no definition" }
}

consteval void
f ()
{
  S<Q> q{};
  (void) q.reflect_constant (); // { dg-error "use of deleted function" }
}
