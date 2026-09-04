// P4222 Initialization profile: real forward "must reach" dataflow
// (ip_compute_reach_info, init-profile-gimple.cc, added 2026-09-04)
// for the CFG-dominance-based DAA path -- used for an [[uninit]]
// local whose address is taken (so it can't use the SSA/PHI-based
// path a plain register-class scalar gets). Assigning it on every
// arm of an if/else and reading it at the merge point is accepted,
// the same way it already is for a plain register-class scalar
// (d4324-profiles-uninit-daa-both-branches-assign-ok.C): plain CFG
// dominance alone can't express this (neither branch's own write
// individually dominates the merge point), which used to make this
// exact shape a false rejection for any non-register [[uninit]]
// variable, even though it's provably safe.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int use (int);

void f (bool c)
{
  [[uninit]] int x;
  int *p [[ref_to_uninit]] = &x; // forces x non-register (address-taken)
  if (c)
    x = 1;
  else
    x = 2;
  use (x);
  (void) p;
}
