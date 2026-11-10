// P4466R0 (FI-067)
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fmodules -freflection" }

#include <meta>
import M;

static_assert (std::meta::identifier_of (^^ns::basic_string) == "basic_string");
static_assert (std::meta::is_class_template (^^ns::basic_string));
static_assert (std::meta::identifier_of (std::meta::parent_of (^^ns::basic_string))
	       == "v1");
static_assert (std::meta::parent_of (^^ns::g) == ^^ns);
constexpr std::meta::info r1 = ^^ns::string;
constexpr std::meta::info r2 = ^^ns::S;
constexpr std::meta::info r3 = ^^ns::g;
constexpr std::meta::info r4 = ^^ns::f;	// { dg-error "overload set" }
constexpr std::meta::info r5 = ^^ns::h;	// { dg-error "cannot be applied to a using-declaration" }
