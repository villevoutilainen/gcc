// P3446R0 Invalidation profile: the moment a constructor's own member-
// initializer EXPLICITLY computes '&r' and hands that address to some
// OTHER type's constructor, that's exactly as suspect as the already-
// caught direct-pointer-field case (d4324-profiles-invalidation-ctor-
// pointer-member-binds-param-bad.C's own 'Z(const int& r) : p(&r)
// {}'), regardless of what the wrapping type's own constructor does
// with it -- 'SomeWrapper' here is only DECLARED, not DEFINED, and
// this must still be flagged at X's own constructor.
//
// Confirmed via -fdump-tree-gimple this is NOT about spotting a
// literal ADDR_EXPR among the call's own arguments: when R is already
// reference-typed, '&r' produces no ADDR_EXPR at all (R's own SSA
// value already IS the address) -- so this check gates on
// SomeWrapper's own DECLARED parameter type being POINTER_TYPE
// instead (the only way '&r' could even type-check in the source),
// not on the argument's own GIMPLE shape.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct SomeWrapper
{
  const int *inner;
  SomeWrapper (const int *p);
};

struct X
{
  SomeWrapper w;
  X (const int &r) : w (&r) {} // { dg-error "may not outlive this object" }
};
