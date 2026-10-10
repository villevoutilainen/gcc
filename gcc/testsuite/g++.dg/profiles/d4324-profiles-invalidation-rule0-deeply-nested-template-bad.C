// P3446R0/P4296R0 Invalidation profile, Rule #0: same as the sibling
// rule0-nested-template-bad.C test, but nested THREE levels deep
// (vector<vector<vector<int>>> vs vector<int>) -- confirms the
// structural-nesting check (ip_type_structurally_nests_p,
// invalidation-profile-gimple.cc) actually RECURSES through nested
// template arguments rather than only checking one level directly.
// Confirmed via a throwaway instrumented build that
// vector<vector<vector<int>>>'s own FIRST template argument is
// vector<vector<int>>, which does NOT directly equal vector<int> --
// a one-level-only containment check would have wrongly cleared this
// exact case, which must stay flagged.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void f (std::vector<std::vector<std::vector<int>>>& outer,
	std::vector<int>& inner)
{
  auto& ref = inner[0];
  outer.push_back ({});
  ref = 42; // { dg-error "potentially invalidated by an earlier mutation" }
}

int main ()
{
  std::vector<std::vector<std::vector<int>>> outer { { {1, 2, 3} } };
  f (outer, outer[0][0]); // outer and inner genuinely alias here
}
