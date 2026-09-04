// P4222 Initialization profile, Phase 4f: real per-FIELD CFG-
// dominance-based DAA for an arbitrary [[uninit]]-marked aggregate
// local (ip_check_local_aggregate_member, init-profile-gimple.cc,
// added 2026-09-04) -- a whole-object [[must_init]] bulk-
// initialization call (fill(&p)) is a recognized initializing event
// for EVERY field, not just for a scalar/array, so p.x is fine to
// read afterward -- matching the array precedent (d4324-profiles-
// array-bulk-init-ok.C's own "uninitialized_fill(a2,10); int x =
// a2[0]; // OK" example) rather than being honestly declined the way
// this exact shape used to be (this file used to be named -bad.C, an
// unconditional decline being the previously correct answer).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct P {
  int x;
  int y;
};

void fill (P* q [[must_init]]);

void f ()
{
  [[uninit]] P p;
  fill (&p);
  int x = p.x;
  (void) x;
}
