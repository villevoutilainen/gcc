// P4340: a defaulted reflect_constant is a defaulted member function. It
// returns the reflection of the template parameter object template-
// argument-equivalent to the object (P4340, 2.2: serialize, then
// deserialize, all members), and is defined as deleted only where its
// class can't be structural; reflection tells which.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }

#include <meta>

class P {
  int v;
public:
  constexpr P (int i) : v (i) {}
  constexpr int get () const { return v; }
  consteval std::meta::info reflect_constant () const = default;
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
static_assert (p.reflect_constant () == std::meta::reflect_constant (p));
static_assert ([: p.reflect_constant () :].get () == 42);
static_assert (pget<([: p.reflect_constant () :])> == 42);
constexpr S<int> s{7};
static_assert (s.reflect_constant () == std::meta::reflect_constant (s));
static_assert ([: s.reflect_constant () :].t == 7);

consteval void
f ()
{
  S<Q> q{};
  (void) q.reflect_constant (); // { dg-error "use of deleted function" }
}
