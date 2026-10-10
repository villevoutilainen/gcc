// P3446R0 Invalidation profile: a second, independently-found gap --
// unlike the sibling d4324-profiles-invalidation-ctor-ref-member-
// full-chain-bad.C (where a dedicated getter's own read trust needed
// a construction-side check to stay sound), a pointer member's own
// read normally stays conservative on its own (no read-site trust was
// ever extended to it). But when the constructed object is returned
// WHOLESALE, with no getter involved at all, the path taken is
// different: the caller's own return-escape check examines the
// CONSTRUCTOR CALL's own arguments directly (trusted, per the named-
// decl reference-binding trust), never reading any member at all --
// so the pointer member's usual safety net never comes into play
// either. Confirmed this compiled clean end-to-end before this fix.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct Z
{
  const int *p;
  Z (const int &r) : p (&r) {} // { dg-error "may not outlive this object" }
};

Z make ()
{
  int local = 7;
  return Z (local);
}
