// Without -freflection-structural-newmetainfo-conversion,
// std::meta::template_object_info isn't declared, and a string literal is not
// a template argument.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }

#include <meta>
#ifdef __GXX_REFLECTION_TEMPLATE_OBJECT_INFO
#error
#endif
std::meta::template_object_info *p;	// { dg-error "has not been declared|does not name a type|not a member" }

template <const char *P> struct X { };
X<"hello"> x;	// { dg-error "string literals can never be used" }
