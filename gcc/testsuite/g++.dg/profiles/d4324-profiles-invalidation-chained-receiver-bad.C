// P3446R0/P4296R0 Invalidation profile: a chained method call whose
// own intermediate result is never bound to a named variable at all
// -- 'v.at(0).push_back(...)' -- is a real false negative this
// checker used to have: at()'s own return value is an anonymous SSA
// temporary with no source-level name, so it was completely invisible
// to Rule #0/#1's use-tracking (ip_use_decl required a named VAR_DECL/
// PARM_DECL behind every SSA operand). The exact same hazard, written
// with a named reference instead (see the sibling -ok test's own
// comment, or d4324-profiles-invalidation-local-reference-bad.C for
// the general pattern), was already caught; only the un-named,
// chained form slipped through. Fixed by letting ip_use_decl fall
// back to the anonymous SSA temporary itself when nothing else
// resolves it, and teaching ip_defines_var_p's reach-info walk that
// such a temporary's own (unique, SSA-guaranteed) definition trivially
// reaches every one of its uses.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void f ()
{
  std::vector<std::vector<int>> v { {1} };
  // at(0) binds an anonymous reference into v's own storage; the
  // comma-operator side effect reallocates v while computing
  // push_back's own argument, before push_back then runs on the
  // now-dangling reference.
  v.at (0).push_back ((v.resize (v.capacity () + 1), 42)); // { dg-error "potentially invalidated" }
}
