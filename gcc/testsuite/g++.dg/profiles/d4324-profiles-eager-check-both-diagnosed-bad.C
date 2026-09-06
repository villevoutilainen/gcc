// P3446R0/P4222 profiles: a front-end error in one function must not
// suppress a GIMPLE-level profile diagnostic in a completely
// unrelated function elsewhere in the same translation unit.  Before
// the eager per-function checking mechanism (expand_or_defer_fn ->
// profiles_eager_check_function, semantics.cc/profiles.cc), GIMPLE
// passes -- including both profile checkers -- were gated on the
// WHOLE translation unit being free of front-end errors
// (pass_build_ssa_passes's own "!seen_error ()" gate, passes.cc), so
// f()'s genuine std::invalidation violation below went completely
// undiagnosed once g()'s unrelated front-end error existed anywhere
// in the same file.  Both are expected here now.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
[[profiles::enforce(std::invalidation)]];

void f (int *p)
{
  delete p; // { dg-error "not marked" }
}

int g ()
{
  int x [[uninit]];
  int *p [[ref_to_uninit]] = &x;
  return *p; // { dg-error "read before it is definitely assigned" }
}

int h ()
{
  return undeclared_identifier; // { dg-error "was not declared" }
}
