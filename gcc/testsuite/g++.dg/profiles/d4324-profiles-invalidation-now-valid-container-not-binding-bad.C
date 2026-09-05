// P3446R0/P4296R0 Invalidation profile: std::now_valid() is strictly
// per-object, not transitive. Calling it on the CONTAINER ('vi') only
// revalidates 'vi' itself -- it does not, and structurally cannot,
// retroactively revalidate a binding ('p') already established to
// 'vi' before this call. Each binding needs its own now_valid() call
// (see d4324-profiles-invalidation-now-valid-binding-ok.C, which calls
// std::now_valid(p) instead and compiles clean).
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
  std::now_valid (vi);
  *p = 7; // { dg-error "potentially invalidated by an earlier mutation" }
}
