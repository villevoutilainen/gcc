// P3446R0/P4296R0 Invalidation profile: constructing and mutating a
// REAL std::vector<int> (not a toy template) must not trip a false
// positive inside libstdc++'s own internal implementation.
// std::vector's initializer-list constructor internally calls
// std::__uninitialized_copy_a, whose own loop returns its 'result'
// parameter incremented via pointer arithmetic through a basic block
// with no valid source location at all (confirmed directly via
// -fdump-tree-ssa: the escape-check's own diagnostic used to fire with
// NO file:line at all, "cc1plus: error: ..." -- because the per-
// statement location it had to work with was invalid, and profiles_
// header_exempt_p's in_system_header_at check can't tell an unknown
// location is inside an exempted header).
// profiles_diagnostic_exempt_p (invalidation-profile-gimple.cc) now
// falls back to the enclosing (instantiated template) function's own
// DECL_SOURCE_LOCATION, which always correctly points at the
// template's own definition site regardless of any specific
// statement's own location.
//
// Real installed/Compiler Explorer usage needs no explicit exemption
// at all (system headers are auto-exempt); this in-tree DejaGnu run
// builds against the not-yet-installed tree via plain '-I', which
// never gets that treatment -- see d4324-profiles-invalidation-no-
// dangling-ok.C's own identical note.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void f (std::vector<int> &vi) { vi.push_back (9); }

void g ()
{
  std::vector<int> vi { 1, 2 };
  f (vi);
}
