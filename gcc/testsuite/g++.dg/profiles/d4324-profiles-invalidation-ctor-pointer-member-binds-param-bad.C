// P3446R0 Invalidation profile: the POINTER-member counterpart of the
// sibling d4324-profiles-invalidation-ctor-ref-member-binds-param-
// bad.C -- storing the ADDRESS of a 'const T&'/'T&&' parameter into a
// pointer member, within its own constructor, must be flagged the
// same way, for the same reason: the member persists for the
// constructed object's own lifetime, which is guaranteed to extend
// past this one constructor call.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct Z
{
  const int *p;
  Z (const int &r) : p (&r) {} // { dg-error "may not outlive this object" }
};
