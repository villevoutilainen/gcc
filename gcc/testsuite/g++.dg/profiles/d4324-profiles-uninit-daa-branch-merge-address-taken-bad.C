// P4222 Initialization profile: the negative control for
// d4324-profiles-uninit-daa-branch-merge-address-taken-ok.C -- only
// ONE arm of the if/else assigns, so the merge-point read still
// isn't safe on every path, and is still rejected.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int use (int);

void f (bool c)
{
  [[uninit]] int x;
  int *p [[ref_to_uninit]] = &x;
  if (c)
    x = 1;
  use (x); // { dg-error "read before it is definitely assigned" }
  (void) p;
}
