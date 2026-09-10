// P4222 Initialization profile: without std::now_init(), a pointer to
// an [[uninit]] local still needs its own [[ref_to_uninit]] flavor to
// read through -- confirms d4324-profiles-uninit-now-init-ok.C's
// clean compile is genuinely due to now_init()'s own effect, not the
// checker being inert here for some other reason.  Taking x's address
// that way makes it unverifiable (the GIMPLE-level assignment-flavor-
// consistency check would report the identical fact for this exact
// direct-&x shape, so it's skipped as pure duplication), and -- with
// no now_init() to cure x -- the eventual read through p is flagged
// too: two independent diagnostics.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int without_now_init ()
{
  int x [[uninit]];
  int *p = &x; // { dg-error "before it is provably initialized" }
  return *p; // { dg-error "read before it is definitely assigned" }
}
