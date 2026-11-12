// Without -freflection-operator-template, operator template is not a name,
// and a string literal is not a template argument.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }

#ifdef __GXX_REFLECTION_OPERATOR_TEMPLATE
#error
#endif
struct A { consteval operator template() const; };	// { dg-error "expected" }

template <const char *P> struct X { };
X<"hello"> x;	// { dg-error "string literals can never be used" }
