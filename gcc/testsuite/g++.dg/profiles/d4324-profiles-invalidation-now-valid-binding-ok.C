// P3446R0/P4296R0 Invalidation profile: std::now_valid() lets a user
// manually assert -- without proof -- that a binding is valid as of
// this exact call, sidestepping this checker's own local, single-hop
// mutation-ordering analysis (invalidation-profile-gimple.cc's own
// recognition of a call to this exact function via ip_defines_var_p/
// ip_binding_established_by). Here, 'p' is bound to 'vi' before 'vi'
// is mutated, but std::now_valid(p) re-establishes p's own binding
// AFTER that mutation, so the subsequent dereference is clean.
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
  *p = 7;
}
