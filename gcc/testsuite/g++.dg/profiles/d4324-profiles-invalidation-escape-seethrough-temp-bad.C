// P3446R0 Invalidation profile: companion to the sibling -ok test --
// the exact same anonymous-temp "see through" recursion must still
// correctly flag a const-lvalue-reference or rvalue-reference
// container parameter, since the referent may be a caller-
// materialized temporary whose lifetime ends by the time this
// function returns. This is the critical regression check for fix
// C's own const-ref/rvalue-ref logic (invalidation-profile-gimple.cc,
// ip_local_var_p), now reached one layer deeper -- through the
// anonymous temp's own reaching-write recursion, then through
// operator[]'s own receiver-argument check -- rather than directly.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

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
