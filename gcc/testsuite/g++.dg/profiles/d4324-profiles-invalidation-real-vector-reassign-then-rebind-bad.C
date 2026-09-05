// P3446R0/P4296R0 Invalidation profile: the identical shape
// d4324-profiles-invalidation-real-vector-reassign-then-rebind-ok.C
// accepts, plus one more mutating call *after* the rebind -- confirms
// the new lower-bound check (invalidation-profile-gimple.cc) doesn't
// overcorrect into never flagging a genuine use-after-mutation once a
// reassignment has occurred earlier in the same function.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void may_invalidate (std::vector<int> &v) { v.push_back (9); }

void g ()
{
  std::vector<int> va { 1, 2 };
  std::vector<int> vb { 1, 2 };

  may_invalidate (vb);
  vb = va;
  auto p = vb.data () + 1;
  may_invalidate (vb);
  *p = 7; // { dg-error "potentially invalidated by an earlier mutation" }
}
