// P3446R0/P4296R0 Invalidation profile: 'vec.data() + n' (a
// POINTER_PLUS_EXPR-shaped assignment, not a plain single-operand
// copy or a GIMPLE_CALL) must still be tracked as bound to 'vec' --
// confirmed directly that without this, ip_binding_established_by
// had no case for a POINTER_PLUS_EXPR-shaped def statement, so the
// recursion into 'vec.data()' itself never even started, and this
// exact shape was silently missed entirely (no diagnostic at all).
// ip_resolve_defining_stmt (invalidation-profile-gimple.cc) now
// resolves a POINTER_PLUS_EXPR's own base operand the same way a
// plain copy's whole RHS already was.
//
// Real installed/Compiler Explorer usage needs no explicit exemption
// at all (system headers are auto-exempt); this in-tree DejaGnu run
// builds against the not-yet-installed tree via plain '-I', which
// never gets that treatment -- see d4324-profiles-invalidation-
// real-vector-construction-ok.C's own identical note.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];
[[profiles::exempt(std::invalidation, angle_header: "vector")]];

#include <vector>

void f (std::vector<int> &vi) { vi.push_back (9); }

void g ()
{
  std::vector<int> vi { 1, 2 };
  auto p = vi.data () + 1;
  f (vi);
  *p = 7; // { dg-error "potentially invalidated by an earlier mutation" }
}
