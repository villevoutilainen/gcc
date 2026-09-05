// P3446R0/P4296R0 Invalidation profile: the identical shape
// d4324-profiles-invalidation-now-valid-binding-ok.C accepts, plus one
// more mutating call *after* std::now_valid(p) -- confirms now_valid()
// only moves a binding's own establishment point forward to the call
// site, it does not disable checking altogether.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <vector>
#include <utility>

void may_invalidate (std::vector<int> &v) { v.push_back (9); }

void g ()
{
  std::vector<int> vi { 1, 2 };
  auto p = vi.data () + 1;
  may_invalidate (vi);
  std::now_valid (p);
  may_invalidate (vi);
  *p = 7; // { dg-error "potentially invalidated by an earlier mutation" }
}
