// P3446R0 Invalidation profile: std::no_escape() lets the programmer
// manually assert -- without proof -- that the reference GCC forms to
// bind a reference parameter is not retained past the call, only the
// value read through it matters. This is the explicit, opt-in
// resolution for the cases in ~/coherent-lifetime-rules-for-dangling.md
// that have no automatic fix (binding any reference parameter to a
// local/parameter requires taking that local's own address, and this
// checker cannot tell that apart from a genuinely retained, escaping
// address without looking into the callee's body): each of these four
// cases is currently flagged without the wrap below (see the sibling
// escape-wrapped-param-bad-equivalent shapes already covered elsewhere
// in this suite), and must stay clean once std::no_escape() is added.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];
[[profiles::exempt(std::invalidation, angle_header: "utility")]];

#include <vector>
#include <utility>

// Case 1: a plain parameter wrapped into a returned container.
std::vector<int*> wrap_incoming_ptr (int* p)
{
  std::vector<int*> v;
  v.push_back (std::no_escape (p));
  return v;
}

// Case 3: an lvalue-reference parameter -- genuinely safe already
// (the referent outlives the call), the wrap is purely to confirm it
// doesn't newly break once a reference-binding temporary is involved.
std::vector<int*> lvalue_ref_param (std::vector<int> &v)
{
  std::vector<int*> ret;
  auto &ref = v[0];
  ret.push_back (std::no_escape (&ref));
  return ret;
}

// Case 7: a raw pointer parameter.
std::vector<int*> raw_ptr_param (std::vector<int> *v)
{
  std::vector<int*> ret;
  auto &ref = (*v)[0];
  ret.push_back (std::no_escape (&ref));
  return ret;
}

// Case 8: a const raw pointer parameter.
std::vector<const int*> const_raw_ptr_param (const std::vector<int> *v)
{
  std::vector<const int*> ret;
  auto &ref = (*v)[0];
  ret.push_back (std::no_escape (&ref));
  return ret;
}
