// P3446R0/P4296R0 Invalidation profile: the identical shape
// d4324-profiles-invalidation-real-vector-data-offset-bad.C rejects,
// minus the intervening mutating call -- confirms that -bad.C's
// rejection is genuinely due to tracking the 'vec.data() + n'
// binding, not 'vec.data() + n' being flagged unconditionally.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void g ()
{
  std::vector<int> vi { 1, 2 };
  auto p = vi.data () + 1;
  *p = 7;
}
