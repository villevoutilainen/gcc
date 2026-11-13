// P4340R1's template-argument function, spelled as a conversion function to
// std::meta::template_object_info
// (-freflection-structural-newmetainfo-conversion): declaring one.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection -freflection-structural-newmetainfo-conversion" }

#include <meta>
#include <type_traits>
#if __GXX_REFLECTION_TEMPLATE_OBJECT_INFO != 1 || defined (__GXX_REFLECTION_OPERATOR_TEMPLATE) || defined (__GXX_REFLECTION_METAINFO_CONVERSION)
#error
#endif
using std::meta::info;
using std::meta::template_object_info;
template <auto> struct T { };
struct A { int i; };
class C {
  int i;
public:
  consteval explicit operator template_object_info() const = default;      // OK
};
struct Mod { int v; consteval explicit operator template_object_info() const; };
template <int V> constexpr Mod canonical = {V};
consteval Mod::operator template_object_info() const {             // out-of-class definition
  return template_object_info (std::meta::substitute (^^canonical, {std::meta::reflect_constant ((v % 7 + 7) % 7)}));
}
struct X1 { consteval explicit operator template_object_info(this X1); };            // OK, explicit object C
struct X2 { consteval explicit operator template_object_info(this const X2&); };     // OK
struct X3 { consteval explicit operator template_object_info() const &; };           // OK
struct G { operator template_object_info() const; };                        // { dg-error "must be declared .consteval.|must be declared .explicit." }
struct H { consteval explicit operator template_object_info(); };                    // { dg-error "object parameter of .operator std::meta::template_object_info. must have type" }
struct J { consteval explicit operator template_object_info() const; };
consteval J::operator template_object_info() const = default;               // { dg-error "can only be defaulted in its class" }
struct M { template <class T> consteval explicit operator template_object_info() const; };  // { dg-error "cannot be a function template" }

// Like operator template, it can't be named outside its declarator: not
// called, using-declared or reflected by name.
struct K : A { using A::operator template_object_info; };          // { dg-error "cannot be named by a using-declarator" }
constexpr info r0 = ^^A::operator template_object_info;           // { dg-error "cannot be named outside the declarator" }
consteval info f (A a) { return info (a.operator template_object_info ()); }   // { dg-error "can only be used to find a template parameter object" }
// members_of lists it: a special member function, explicit.
consteval info tafn (info c) {
  for (info m : std::meta::members_of (c, std::meta::access_context::unchecked ()))
    if (std::meta::is_conversion_function (m))
      return m;
  return info ();
}
static_assert (std::meta::is_special_member_function (tafn (^^A)));
static_assert (std::meta::is_explicit (tafn (^^A)));
// A user-provided one, listed once: the user's, not an implicit one too.
consteval int ntafn (info c) {
  int n = 0;
  for (info m : std::meta::members_of (c, std::meta::access_context::unchecked ()))
    n += std::meta::is_conversion_function (m);
  return n;
}
static_assert (ntafn (^^Mod) == 1);
static_assert (std::meta::is_special_member_function (tafn (^^Mod)));
static_assert (std::meta::is_explicit (tafn (^^Mod)));
static_assert (std::meta::is_user_provided (tafn (^^Mod)));
static_assert (!std::meta::is_defaulted (tafn (^^Mod)));
// Nor used otherwise: it only finds a template parameter object.
static_assert (!std::is_convertible_v<A, template_object_info>);
consteval info c1 () { return info (static_cast<template_object_info> (Mod{10})); }  // { dg-error "can only be used to find a template parameter object" }
consteval info c2 () { template_object_info t (A{1}); return t; }                   // { dg-error "can only be used to find a template parameter object" }
static_assert (std::is_same_v<T<Mod{3}>, T<Mod{10}>>);
// A user-declared one must be explicit too.
struct E1 { int i; consteval explicit operator template_object_info () const = default; };
static_assert (!std::is_convertible_v<E1, template_object_info>);
static_assert (std::is_same_v<T<E1{1}>, T<E1{1}>>);
struct E2 { int i; consteval operator template_object_info () const = default; };   // { dg-error "must be declared .explicit." }

// Only a non-template conversion to exactly the type is the
// template-argument function: a generic one isn't.
struct P { int i; template <class T> constexpr operator T () const { return T (); } };
T<P{1}> tp;
static_assert (!std::is_same_v<T<P{1}>, T<P{2}>>);
// A base's isn't the derived class's: it has its own, implicitly, and its
// Mod subobject is normalized to a copy of Mod's template parameter object.
struct D : Mod { };
static_assert (std::is_same_v<T<D{{3}}>, T<D{{10}}>>);
static_assert (std::is_same_v<T<Mod{3}>, T<Mod{10}>>);
static_assert (std::meta::parent_of (std::meta::members_of (^^D, std::meta::access_context::unchecked ()).back ()) == ^^D);

// Quacks like operator template: nothing can construct a
// template_object_info from it, extract can't take it, and deleting it opts
// its class out.
static_assert (!std::is_constructible_v<template_object_info, A>);
static_assert (!std::is_constructible_v<template_object_info, Mod>);
consteval bool extract_throws () {
  try { (void) std::meta::extract<template_object_info (A::*) () const> (tafn (^^A)); }
  catch (std::meta::exception &) { return true; }
  return false;
}
static_assert (extract_throws ());
struct Z { int i; consteval explicit operator template_object_info () const = delete; };
T<Z{1}> tz;                                                        // { dg-error "not a valid type|not structural|structural" }

// What a template-argument function gives isn't a template argument.
template <template_object_info> struct U { };                      // { dg-error "not a valid type for a template non-type parameter|structural" }

// A reflection doesn't convert implicitly.
consteval void g () { template_object_info t = ^^A; }               // { dg-error "conversion from" }
consteval void h () { template_object_info t (^^A); (void) info (t); } // OK
