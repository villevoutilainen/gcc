// P3446R0/P4296R0 Invalidation profile: returning an [[owner]]
// parameter through a plain, non-owner-marked return type is a
// harmless "peek" from the flavor-consistency layer's own point of
// view (not checked at all -- see ip_check_owner_return_flavor_
// consistency's own comment, invalidation-profile-gimple.cc), but it
// does NOT satisfy CE3, which requires the function's own return type
// to be owner-marked -- so p is still genuinely never consumed.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int* returned_unflavored_bad ([[owner]] int *p) // { dg-error "never deleted or passed on" }
{
  return p;
}
