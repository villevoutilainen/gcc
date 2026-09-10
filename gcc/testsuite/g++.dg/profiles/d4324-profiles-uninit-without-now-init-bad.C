// P4222 Initialization profile: without std::now_init(), a pointer to
// an [[uninit]] local still needs its own [[ref_to_uninit]] flavor to
// read through -- confirms d4324-profiles-uninit-now-init-ok.C's
// clean compile is genuinely due to now_init()'s own effect, not the
// checker being inert here for some other reason.  The mismatched
// initialization also trips the GIMPLE-level assignment-flavor-
// consistency check, taking x's address that way makes it
// unverifiable, and -- with no now_init() to cure x -- the eventual
// read through p is flagged too: three independent diagnostics.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int without_now_init ()
{
  int x [[uninit]];
  int *p = &x; // { dg-error "assigning a pointer marked" }
  // { dg-error "before it is provably initialized" "" { target *-*-* } .-1 }
  return *p; // { dg-error "read before it is definitely assigned" }
}
