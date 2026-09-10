// P4222 Initialization profile: companion to d4324-profiles-uninit-
// flavor-through-pointer-var-bad.C -- once x is cured, the SAME
// pointer-variable chain (p, then q, then a call) is clean end to end.
// This is the "grand unification" property confirmed during this
// fix's own investigation: ip_arg_uninit_flavored_p_1's existing
// recursive SSA-copy chase reaches the DAA-aware &x rule regardless of
// how many plain-copy hops (q <- p <- &x) sit in between, so
// Assign-flavor-mismatch and Call-arg-flavor-mismatch both already get
// the same proof a direct &x would -- no code change was needed for
// this part, only for the COMPONENT_REF/redundancy fixes alongside it.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void other (int* q);

void f ()
{
  [[uninit]] int x;
  int* p [[ref_to_uninit]] = &x;
  x = 5;
  int* q = p;
  other (q);
}
