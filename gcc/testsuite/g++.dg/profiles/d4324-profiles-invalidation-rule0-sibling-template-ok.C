// P3446R0/P4296R0 Invalidation profile, Rule #0: two DIFFERENT
// specializations of the SAME template family, where NEITHER nests
// the other (vector<int> and vector<double> are siblings -- int and
// double are scalar template arguments, not class types, so neither
// specialization's own template-argument tree can ever reach the
// other), are now provably unrelated and must stay clean --
// previously, EVERY pair of specializations of the same template was
// unconditionally treated as "not provably unrelated" regardless of
// whether genuine nesting was possible (see d4324-profiles-
// invalidation-rule0-nested-template-bad.C for the case that must
// stay conservative: vector<vector<int>> DOES nest vector<int>).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void f (std::vector<int>& a, std::vector<double>& b)
{
  auto& ref = b[0];
  a.push_back (42);
  ref = 3.14;
}

int main ()
{
  std::vector<int> a { 1, 2, 3 };
  std::vector<double> b { 1.0, 2.0 };
  f (a, b);
}
