// P4340R1, as a conversion to std::meta::template_object_info
// (-freflection-structural-newmetainfo-conversion): implicitly-declared, defaulted and deleted template-argument
// functions, and members_of.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection -freflection-structural-newmetainfo-conversion" }

#include <meta>
#include <type_traits>
using namespace std::meta;
consteval info tafn (info c) {
  for (info m : members_of (c, access_context::unchecked ()))
    if (is_special_member_function (m) && is_function (m)
        && !is_constructor (m) && !is_destructor (m) && !is_assignment (m))
      return m;
  return info ();
}
struct A { int i; };
class B { int i; };
class C { int i; public: consteval explicit operator template_object_info() const = default; };
struct D { consteval explicit operator template_object_info() const = default; private: B b; };
struct E : C { };
struct F { union { int i; float f; }; };
static_assert (tafn (^^A) != info () && !is_deleted (tafn (^^A)));
static_assert (is_deleted (tafn (^^B)));
static_assert (!is_deleted (tafn (^^C)));
static_assert (is_deleted (tafn (^^D)));
static_assert (!is_deleted (tafn (^^E)));
static_assert (!is_deleted (tafn (^^F)));
template <auto> struct T {};
T<A{1}> ta;
T<C{}> tc;
T<E{}> te;
T<F{}> tf;
// The paper's members_of example.
class BB {};
struct S : BB { private: class I; public: int m; };
static_assert (members_of (^^S, access_context::current ()).size () == 8);
static_assert (members_of (^^S, access_context::unchecked ()).size () == 9);
struct K : A { using A::operator template_object_info; };   // { dg-error "cannot be named by a using-declarator" }
