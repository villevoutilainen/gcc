// P3446R0 Invalidation profile: a genuine false negative/positive gap
// found via https://godbolt.org/z/d9n9xfq5b -- 'return {p};' (an
// initializer-list construction with a POINTER-typed element) used to
// stay conservatively flagged even though 'p' is a plain, non-
// reference parameter, exactly as safe as the already-established
// 'v.push_back(p);' shape right above it.
//
// Confirmed via -fdump-tree-gimple this lowers to a backing array
// 'int* const D.xxx[1]; D.xxx[0] = p;' feeding std::initializer_list's
// own '_M_array'/'_M_len' fields, then vector's initializer-list
// constructor -- structurally identical to the already-resolved
// d4324-profiles-invalidation-escape-seethrough-initializer-list-
// ok.C's own 'return {*x, *y};', EXCEPT that test's backing array
// holds plain 'int' (can't hold a pointer, so ip_type_may_hold_
// pointer_p's own type gate resolves it safe immediately, without
// ever needing to trace into the array's own per-element writes).
// Here the backing array holds 'int*' (CAN hold a pointer), so the
// type gate doesn't short-circuit, and tracing was needed -- but
// ip_collect_component_writes_before (the helper that finds "what was
// written here" for a field/element-by-element-populated temp) only
// ever matched a COMPONENT_REF lhs ('obj.field = ...'), never an
// ARRAY_REF lhs ('arr[i] = ...'), so 'D.xxx[0] = p;' was never found,
// and the search fell to its own "nothing found" conservative
// default-deny. Fixed by teaching that helper to also recognize an
// ARRAY_REF write into the same variable.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

std::vector<int*> f (int* p)
{
  std::vector<int*> ret;
  ret.push_back (p);
  return ret;
}

std::vector<int*> f2 (int* p)
{
  return {p};
}
