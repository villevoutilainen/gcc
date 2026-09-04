// P3446R0 Invalidation profile: a function must not return a pointer
// to one of its own locals (CppCon 2026 "Profiles" talk, slide 45).
// Checked at finish_return_stmt (semantics.cc), consulting the same
// "dangling" flag GCC's own pre-existing -Wreturn-local-addr warning
// already computes -- not at the GIMPLE level: genericize/gimplify
// already replaces a direct "return &local;" with a null-pointer
// constant before any GIMPLE pass runs (confirmed via a direct
// -fdump-tree-ssa-details reading during development, not assumed),
// so the front end is the only point where the real expression is
// still available to check.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int *f ()
{
  int x = 7;
  return &x; // { dg-error "pointer or reference to a local" }
              // { dg-warning "address of local variable" "" { target *-*-* } .-1 }
}
