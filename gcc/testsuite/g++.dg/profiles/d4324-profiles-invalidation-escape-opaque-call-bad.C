// P3446R0/P4296R0 Invalidation profile: a free function's own return
// value, elided directly into the caller's hidden return-value slot
// (NRVO chaining a non-trivially-copyable return type), must still be
// traced into -- its own arguments are exactly as relevant as an
// ordinary call's. Here 'g's body is invisible, but 'ref' traces to a
// reference into a local by-value parameter's own storage, so 'g's
// escaping through its own argument must be assumed, not ruled out for
// lack of proof: the checker never reads a callee's body, so "might
// escape" and "proven to escape" are treated alike whenever an
// argument traces to something this fragile.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

std::vector<int*> g (int&);

std::vector<int*> f (std::vector<int> v)
{
  auto &ref = v[0];
  std::vector<int*> ret = g (ref);
  return ret; // { dg-error "may hold a pointer to a local" }
}
