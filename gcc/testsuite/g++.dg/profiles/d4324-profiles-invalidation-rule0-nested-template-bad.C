// P3446R0/P4296R0 Invalidation profile, Rule #0: two DIFFERENT
// specializations of the SAME template family must stay conservative
// (flagged) when one genuinely nests the other -- vector<vector<int>>
// contains vector<int> as its own element type, so mutating/
// reallocating the outer container can genuinely invalidate a
// reference into one of its own elements, if that reference was
// handed in aliasing one of them. Confirmed via gdb this is exactly
// the call-site shape that makes this real, not hypothetical: outer
// and inner genuinely alias at the call in main() below.  Companion
// to d4324-profiles-invalidation-rule0-sibling-template-ok.C, which
// shows the SAME family comparison correctly clearing a non-nesting
// pair of specializations (vector<int>/vector<double>) that this
// exact nesting check must not also clear.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void f (std::vector<std::vector<int>>& outer, std::vector<int>& inner)
{
  auto& ref = inner[0];
  outer.push_back ({1, 2, 3});
  ref = 42; // { dg-error "potentially invalidated by an earlier mutation" }
}

int main ()
{
  std::vector<std::vector<int>> outer { {1, 2, 3} };
  f (outer, outer[0]); // outer and inner genuinely alias here
}
