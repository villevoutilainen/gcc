// P3446R0 Invalidation profile: contrast cases for the sibling
// d4324-profiles-invalidation-ctor-ref-member-binds-param-bad.C --
// both must stay clean.
//
// 'PlainRef': a plain, non-const, non-rvalue 'T&' parameter cannot
// bind to a temporary at all (a language rule), so binding a
// reference member to it directly carries none of the risk a 'const
// T&'/'T&&' parameter's binding does -- symmetric with how this file
// already trusts reading a plain reference PARAMETER directly, and
// reading a plain reference MEMBER directly (see d4324-profiles-
// invalidation-escape-ref-member-direct-ok.C).
//
// 'Wrapped': the explicit, opt-in 'std::no_dangling' escape hatch
// works here exactly as it does for an ordinary return value -- the
// programmer has independently verified this specific binding is
// safe.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <utility>

struct PlainRef
{
  int &ohnoes;
  PlainRef (int &r) : ohnoes (r) {}
};

struct Wrapped
{
  const int &ohnoes;
  Wrapped (const int &r) : ohnoes (std::no_dangling (r)) {}
};
