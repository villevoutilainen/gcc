// Sibling of d4324-profiles-invalidation-owner-clobber-push-back-ok.C:
// same push_back shape, but p is genuinely never consumed. Must produce
// exactly the one legitimate "never deleted or passed on" diagnostic,
// correctly anchored at p's own declaration -- not the clobber-
// triggered duplicated/unanchored pile this used to produce, and not
// silence either (the real leak must still be caught).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void f ()
{
  std::vector<int *> v;
  v.reserve (4);
  int *p [[owner]] = new int{9}; // { dg-error "never deleted or passed on" }
  v.push_back (p);
}
