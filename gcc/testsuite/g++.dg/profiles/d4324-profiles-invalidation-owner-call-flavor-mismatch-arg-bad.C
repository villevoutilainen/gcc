// P3446R0/P4296R0 Invalidation profile: passing an [[owner]] pointer
// to a call argument whose corresponding parameter is NOT marked
// [[owner]] is an ordinary, harmless "peek" (no longer a flavor
// mismatch of its own -- see ip_check_owner_call_flavor_consistency's
// own comment, invalidation-profile-gimple.cc), but it does NOT
// satisfy the definite-consumption layer's own requirement either:
// sink's parameter isn't an owner-accepting sink, so p is still
// genuinely never consumed.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void sink (int *q);

void f ([[owner]] int *p) // { dg-error "never deleted or passed on" }
{
  sink (p);
}
