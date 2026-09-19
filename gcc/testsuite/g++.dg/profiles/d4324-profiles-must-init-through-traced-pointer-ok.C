// P4222 Initialization profile, Phase 3: the flagship dominance test
// (d4324-profiles-must-init-dominance-ok.C) already confirms passing
// '&x' directly to a [[must_init]] parameter cures x. This confirms the
// identical case still works when x's address reaches the call through
// an intermediate pointer variable first ('int* p [[ref_to_uninit]] =
// &x; f(p);') instead of a literal '&x' argument -- confirmed a real
// gap: ip_scan_stmt_for_var's own must_init-call recognition
// (init-profile-gimple.cc) used to require a literal ADDR_EXPR
// argument, missing this shape entirely, so f(p) was never recognized
// as curing x at all.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void f (int *p [[must_init]]);

int g ()
{
  [[uninit]] int x;
  int *p [[ref_to_uninit]] = &x;
  f (p);
  return x;
}
