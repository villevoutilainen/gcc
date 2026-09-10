// P4222 Initialization profile: curing [[uninit]] (see the companion
// d4324-profiles-uninit-cured-by-write-ok.C) genuinely reuses this
// checker's own CFG-dominance dataflow, not a simplistic "any write
// exists somewhere in the function" shortcut -- a write on only ONE
// branch of a conditional does not dominate a later escape/flavor
// check reached from both branches, so it is still flagged, exactly
// as an equivalent conditional write still leaves a later read
// flagged.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void take_ptr (int *);

void f (bool cond)
{
  int x [[uninit]];
  if (cond)
    x = 5;
  take_ptr (&x); // { dg-error "before it is provably initialized" }
}
