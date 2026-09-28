// Reported (via a paper cross-check): passing an [[owner]] pointer to
// vector<T*>::push_back generated a pile of garbled, duplicated,
// unanchored diagnostics. Root cause: early inlining of push_back's
// call chain introduces scope-exit CLOBBER statements for p inside f's
// own gimplified body, which ip_check_owner_assign_flavor_consistency
// and ip_defines_var_p (used by ip_check_owner_binding's reassignment
// detection) both misread as real reassignments of p. push_back's own
// declared parameter is a plain non-owner reference, and an owner may
// legally alias into a non-owner, so once p is properly consumed
// afterward this must compile fully clean.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void f ()
{
  std::vector<int *> v;
  v.reserve (4);
  int *p [[owner]] = new int{9};
  v.push_back (p);
  delete p;
}
