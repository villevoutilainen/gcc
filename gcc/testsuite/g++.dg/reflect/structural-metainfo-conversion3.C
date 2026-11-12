// Without -freflection-structural-metainfo-conversion, a conversion
// function to std::meta::info has no special meaning.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }

#include <meta>

#ifdef __GXX_REFLECTION_METAINFO_CONVERSION
#error
#endif

struct S1
{
  consteval explicit operator std::meta::info () const = default;	// { dg-error "cannot be defaulted" }
};

class S2
{
  int x;
public:
  consteval S2 (int v) : x (v) {}
  consteval explicit operator std::meta::info () const { return ^^int; }
};
template <S2 V> struct A {};	// { dg-error "not a valid type for a template non-type parameter" }
