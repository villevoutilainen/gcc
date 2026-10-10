// P3446R0 Invalidation profile: contrast cases for the sibling
// d4324-profiles-invalidation-ctor-pointer-member-binds-param-bad.C --
// both must stay clean.
//
// 'PlainPtr': a plain, non-const, non-rvalue 'T&' parameter cannot
// bind to a temporary at all (a language rule), so taking its address
// and storing it into a pointer member carries none of the risk a
// 'const T&'/'T&&' parameter's binding does.
//
// 'WrappedPtr': the explicit, opt-in 'std::no_dangling' escape hatch
// works here exactly as it does for an ordinary pointer value -- the
// programmer has independently verified this specific address is
// safe to store.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <utility>

struct PlainPtr
{
  int *p;
  PlainPtr (int &r) : p (&r) {}
};

struct WrappedPtr
{
  const int *p;
  WrappedPtr (const int &r) : p (std::no_dangling (&r)) {}
};
