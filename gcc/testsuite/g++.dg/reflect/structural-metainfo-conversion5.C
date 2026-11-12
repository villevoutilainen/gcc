// -freflection-structural-metainfo-conversion: a conversion function of
// dependent type is one to std::meta::info only in some specializations;
// each specialization is marked from its own members.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection -freflection-structural-metainfo-conversion" }

#include <meta>
#include <type_traits>

// User-provided, of dependent type.
template <typename R> class Wrap;
template <typename R> constexpr Wrap<R> wrap_canon {};
template <typename R>
class Wrap
{
  int x = 0;
public:
  consteval explicit operator R () const { return ^^wrap_canon<R>; }
};
template <Wrap<std::meta::info> W> struct A {};
static_assert (std::is_same_v<A<Wrap<std::meta::info>{}>, A<Wrap<std::meta::info>{}>>);
template <Wrap<int> W> struct A5 {};	// { dg-error "not a valid type for a template non-type parameter" }

// Defaulted, of dependent type.
template <typename R>
class D		// { dg-error "cannot be defaulted" }
{
  int x;
public:
  consteval D (int v) : x (v) {}
  consteval explicit operator R () const = default;
};
template <D<std::meta::info> V> struct B {};
static_assert (!std::is_same_v<B<D<std::meta::info> (1)>,
			       B<D<std::meta::info> (2)>>);
template class D<int>;

// Defaulted, of dependent type, deleted in a specialization that can't be
// structural.
struct NotStructural { mutable int m; };
template <typename R, typename T>
class E
{
  T t;
public:
  consteval explicit operator R () const = default;
};
static_assert (!std::meta::is_deleted (^^E<std::meta::info, int>::operator std::meta::info));
static_assert (std::meta::is_deleted (^^E<std::meta::info, NotStructural>::operator std::meta::info));
