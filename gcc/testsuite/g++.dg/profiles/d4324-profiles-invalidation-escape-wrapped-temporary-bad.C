// P3446R0/P4296R0 Invalidation profile, per the user's own
// coherent-lifetime-rules-for-dangling.md: wrapping a reference into a
// container parameter and returning it must be flagged when that
// parameter could itself be bound to a caller-materialized temporary --
// by-value (invisible-reference-return ABI convention), 'const T&', and
// 'T&&' all qualify, since the temporary's lifetime ends by the time
// this function returns at the latest. A plain, non-const 'T&' cannot
// bind an rvalue at all, so it alone stays safe (see the sibling -ok
// test covering that and the raw-pointer cases).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

std::vector<int*> by_value_param (std::vector<int> v)
{
  std::vector<int*> ret;
  auto &ref = v[0];
  ret.push_back (&ref);
  return ret; // { dg-error "may hold a pointer to a local" }
}

std::vector<const int*> const_lvalue_ref_param (const std::vector<int> &v)
{
  std::vector<const int*> ret;
  auto &ref = v[0];
  ret.push_back (&ref);
  return ret; // { dg-error "may hold a pointer to a local" }
}

std::vector<int*> rvalue_ref_param (std::vector<int> &&v)
{
  std::vector<int*> ret;
  auto &ref = v[0];
  ret.push_back (&ref);
  return ret; // { dg-error "may hold a pointer to a local" }
}
