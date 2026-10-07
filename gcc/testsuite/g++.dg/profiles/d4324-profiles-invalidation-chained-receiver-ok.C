// P3446R0/P4296R0 Invalidation profile: the sibling -bad test's own
// chained, un-named accessor form ('v.at(0).push_back(...)'), but
// with no mutation of v intervening between the accessor call and the
// chained use -- must stay clean. Confirms the fix that made such a
// chained call visible to Rule #0/#1's use-tracking at all only adds
// *visibility* of the use, not a blanket new diagnostic: the existing
// mutation-ordering dataflow still has to find a genuine intervening
// mutation before it flags anything.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void f ()
{
  std::vector<std::vector<int>> v { {1} };
  v.at (0).push_back (42); // no intervening mutation of v -- fine.
}
