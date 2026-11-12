// P4340R1: operator template in the reflection library: op_template,
// symbol_of, is_operator_function, and extract refusing it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection -freflection-operator-template" }

#include <meta>
#include <string_view>
using namespace std::meta;
struct A { int i; };
consteval info tafn (info c) {
  for (info m : members_of (c, access_context::unchecked ()))
    if (is_operator_function (m) && operator_of (m) == operators::op_template)
      return m;
  return info ();
}
static_assert (tafn (^^A) != info ());
static_assert (is_special_member_function (tafn (^^A)));
static_assert (symbol_of (operators::op_template) == std::string_view ("template"));
static_assert (symbol_of (operators::op_comma) == std::string_view (","));
consteval bool extract_throws () {
  try { (void) extract<info (A::*) () const> (tafn (^^A)); } catch (exception &) { return true; }
  return false;
}
static_assert (extract_throws ());
constexpr info s = reflect_constant (A{1});   // OK
