// P3446R0 Invalidation profile: a real precision fix, not a soundness
// trade-off. Binding a reference into an anonymous, compiler-
// synthesized temporary (here, materializing the computed address of
// a LOCAL reference variable, so it can be passed to push_back's own
// reference parameter) used to be treated as unconditionally "local"
// -- the ADDR_EXPR branch asked only "is the temp's own slot local"
// (always, uselessly, true for any VAR_DECL), never what was actually
// stored into it. The fix recurses into the temp's own reaching
// write instead, applying the SAME, already-existing escape logic to
// what it actually holds. For a plain T&/T*/const T* container
// parameter, that logic (confirmed via gdb) already proves the
// result safe -- no std::no_escape() needed at all.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

// Case 3: plain lvalue-reference container parameter.
std::vector<int*> lvalue_ref_param (std::vector<int> &v)
{
  std::vector<int*> ret;
  auto &ref = v[0];
  ret.push_back (&ref);
  return ret;
}

// Case 7: raw pointer container parameter.
std::vector<int*> raw_ptr_param (std::vector<int> *v)
{
  std::vector<int*> ret;
  auto &ref = (*v)[0];
  ret.push_back (&ref);
  return ret;
}

// Case 8: const raw pointer container parameter.
std::vector<const int*> const_raw_ptr_param (const std::vector<int> *v)
{
  std::vector<const int*> ret;
  auto &ref = (*v)[0];
  ret.push_back (&ref);
  return ret;
}
