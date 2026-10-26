// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }
// reflect_constant can't be both defaulted and deleted (P4340): the second
// declaration is rejected, and has no effect on whether its class is
// structural.

#include <meta>

struct A // { dg-bogus "not a structural type" }
{
  int x;
  consteval std::meta::info reflect_constant () const = default;
  consteval std::meta::info reflect_constant () const = delete; // { dg-error "cannot be overloaded" }
};
static_assert (std::meta::is_structural_type (^^A));

struct B // { dg-bogus "not a structural type" }
{
  int x;
  consteval std::meta::info reflect_constant () const = delete;
  consteval std::meta::info reflect_constant () const = default; // { dg-error "cannot be overloaded" }
};
static_assert (!std::meta::is_structural_type (^^B));

// Deleted after its first declaration: not an opt-out.
struct C
{
  int x;
  consteval std::meta::info reflect_constant () const;
};
consteval std::meta::info C::reflect_constant () const = delete; // { dg-error "not first declaration" }
static_assert (std::meta::is_structural_type (^^C));

struct D
{
  int x;
  consteval std::meta::info reflect_constant () const = delete;
};
consteval std::meta::info D::reflect_constant () const = default; // { dg-error "redefinition" }
static_assert (!std::meta::is_structural_type (^^D));
