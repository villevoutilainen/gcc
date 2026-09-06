// P3446R0/P4296R0 Invalidation profile: same shape as
// d4324-profiles-invalidation-owner-returned-ok.C, but f's own return
// is NOT marked [[owner]] -- the flavor-consistency layer exempts a
// direct pass-through of an already-owner-declared parameter (same
// exemption init-profile-gimple.cc's ip_check_return_flavor_
// consistency has for [[ref_to_uninit]]), so this does NOT trip a
// flavor mismatch.  It must still be flagged as a genuine leak by the
// definite-consumption layer: the caller now silently owns p with no
// [[owner]] marker on f's own return saying so.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int* f ([[owner]] int *p) // { dg-error "never deleted or passed on" }
{
  return p;
}
