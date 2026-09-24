// Negative control for d4324-profiles-owner-return-omission-new-bad.C:
// the identical body, but with [[owner]] correctly on the return type.
// Must stay clean -- confirms no double-diagnosis with the existing,
// opposite-direction ip_check_owner_return_flavor_consistency, and that
// ip_check_owner_return_omission correctly exempts a properly-marked
// function via its own early profiles_owning_ptr_p(enclosing_fndecl)
// check.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

[[owner]] int *
f ()
{
  return new int{9};
}
