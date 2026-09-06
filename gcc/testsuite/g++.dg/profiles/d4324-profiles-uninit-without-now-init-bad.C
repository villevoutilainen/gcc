// P4222 Initialization profile: without std::now_init(), a pointer to
// an [[uninit]] local still needs its own [[ref_to_uninit]] flavor to
// read through -- confirms d4324-profiles-uninit-now-init-ok.C's
// clean compile is genuinely due to now_init()'s own effect, not the
// checker being inert here for some other reason.  The mismatched
// initialization also trips the GIMPLE-level assignment-flavor-
// consistency check, and taking x's address that way makes it
// unverifiable -- both now fire alongside the front-end declaration-
// time error, since the eager per-function checking mechanism no
// longer lets it suppress them.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int without_now_init ()
{
  int x [[uninit]]; // { dg-error "cannot verify" }
  int *p = &x; // { dg-error "which is marked" }
  // { dg-error "assigning a pointer marked" "" { target *-*-* } .-1 }
  return *p;
}
