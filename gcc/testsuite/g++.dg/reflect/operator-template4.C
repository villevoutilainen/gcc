// Without -freflection-operator-template, operator template is not a name.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }

#ifdef __GXX_REFLECTION_OPERATOR_TEMPLATE
#error
#endif
struct A { consteval operator template() const; };	// { dg-error "expected" }
