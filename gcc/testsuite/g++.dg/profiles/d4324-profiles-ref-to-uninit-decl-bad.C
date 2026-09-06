// P4222 Initialization profile, Phase 3: [[ref_to_uninit]] flavor must
// match the pointee's [[uninit]] status in both directions.  Each
// mismatched initialization also trips the GIMPLE-level checker's own
// assignment-flavor-consistency check, and x2's address being taken by
// the mismatched p2 initialization makes it unverifiable -- all three
// now fire alongside the front-end declaration-time errors, since the
// eager per-function checking mechanism no longer lets an earlier
// front-end error in this same function suppress them.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void f ()
{
  int x1 = 7;
  int* p1 [[ref_to_uninit]] = &x1; // { dg-error "but .x1. is not marked" }
  // { dg-error "assigning a pointer not marked" "" { target *-*-* } .-1 }

  [[uninit]] int x2; // { dg-error "cannot verify" }
  int* p2 = &x2; // { dg-error "points to .x2., which is marked" }
  // { dg-error "assigning a pointer marked" "" { target *-*-* } .-1 }

  (void) p1;
  (void) p2;
}
