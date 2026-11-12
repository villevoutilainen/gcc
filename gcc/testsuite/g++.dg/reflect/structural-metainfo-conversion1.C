// -freflection-structural-metainfo-conversion: a defaulted conversion
// function to std::meta::info, possibly explicit, takes the place of a
// defaulted reflect_constant (P4340).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection -freflection-structural-metainfo-conversion" }

#include <meta>
#include <type_traits>

#if __GXX_REFLECTION_METAINFO_CONVERSION != 1
#error
#endif

// Structural regardless of the access of its members; member-wise
// template-argument-equivalence.
class P
{
  int a, b;
public:
  consteval P (int x, int y) : a (x), b (y) {}
  consteval explicit operator std::meta::info () const = default;
};
template <P V> struct A {};
static_assert (std::is_same_v<A<P (1, 2)>, A<P (1, 2)>>);
static_assert (!std::is_same_v<A<P (1, 2)>, A<P (1, 3)>>);

// Callable: it gives what std::meta::reflect_constant gives.
static_assert (static_cast<std::meta::info> (P (1, 2))
	       == std::meta::reflect_constant (P (1, 2)));

// Not explicit is fine too.
class Q
{
  int a;
public:
  consteval Q (int x) : a (x) {}
  consteval operator std::meta::info () const = default;
};
template <Q V> struct B {};
static_assert (std::is_same_v<B<Q (1)>, B<Q (1)>>);
static_assert (std::meta::info (Q (1)) == std::meta::reflect_constant (Q (1)));

// Deleting it opts out.
struct R
{
  int a;
  consteval explicit operator std::meta::info () const = delete;
};
template <R V> struct C {};	// { dg-error "not a valid type for a template non-type parameter" }

// In a class template, the defaulted one is deleted where a specialization
// can't be structural.
struct NotStructural { mutable int m; };
template <typename T>
class Box
{
  T t;
public:
  consteval explicit operator std::meta::info () const = default;
};
static_assert (!std::meta::is_deleted (^^Box<int>::operator std::meta::info));
static_assert (std::meta::is_deleted (^^Box<NotStructural>::operator std::meta::info));
template <Box<int> V> struct D {};

// The shape: consteval, const, no ref-qualifier.
struct S1
{
  explicit operator std::meta::info () const = default;	// { dg-error "must be a .consteval. .const. member function" }
};
struct S2
{
  consteval explicit operator std::meta::info () = default;	// { dg-error "must be a .consteval. .const. member function" }
};
struct S3
{
  consteval explicit operator std::meta::info () const & = default;	// { dg-error "no ref-qualifier" }
};

// A member named reflect_constant is an ordinary function.
struct T1
{
  consteval std::meta::info reflect_constant () const = default;	// { dg-error "cannot be defaulted" }
};
class T2
{
  int a;
public:
  consteval std::meta::info reflect_constant () const = delete;
};
template <T2 V> struct E {};	// { dg-error "not a valid type for a template non-type parameter" }
struct T3
{
  int a;
  consteval std::meta::info reflect_constant () const = delete;
};
template <T3 V> struct F {};	// T3 is structural anyway.
