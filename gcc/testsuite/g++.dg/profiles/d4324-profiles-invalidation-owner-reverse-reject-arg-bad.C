// P3446R0/P4296R0 Invalidation profile: passing an arbitrary,
// untracked raw pointer into an owner-*accepting* call-argument
// position is still rejected -- the reverse direction of the
// call-argument flavor check, unaffected by the forward direction's
// own removal (see ip_check_owner_call_flavor_consistency's own
// comment, invalidation-profile-gimple.cc).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void sink ([[owner]] int *q);

void reverse_reject_arg_bad (int *arbitrary)
{
  sink (arbitrary); // { dg-error "must be marked" }
}
