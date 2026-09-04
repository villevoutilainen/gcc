// P4222 Initialization profile, Phase 4d (S5.1-S5.3): two genuinely
// distinct kinds of member-level errors, checked through 'this'
// instead of a plain variable (Phase 3's own address-taken-local
// DAA).
//
// A literally-[[uninit]]-marked member is never required to be
// assigned by the constructor (see member-body-daa-ok.C) -- but a
// read of it BEFORE any write, within the same constructor body, is
// still exactly the kind of provably-unsound access real CFG-
// dominance-based DAA exists to catch.
//
// A [[ref_to_uninit]]/[[must_init]]-marked member is different: it's
// a pointer/reference whose POINTEE may be uninitialized, not the
// pointer itself -- the pointer's own VALUE still has to exist before
// the object is exposed to callers, so leaving it unassigned on any
// exit path is still rejected, the same as any other, unflavored
// member would be if left out of the member-initializer-list/NSDMI
// entirely.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct ReadBeforeWrite {
  int* p [[uninit]];
  int x;
  ReadBeforeWrite (int v) : x{v}
  {
    int* q = p; // { dg-error "read before it is definitely assigned" }
    p = new int(v);
    (void) q;
  }
};

struct RefToUninitMemberNeverAssigned {
  int m1;
  int* m2 [[ref_to_uninit]];
  RefToUninitMemberNeverAssigned (int x) : m1{x} {} // { dg-error "not definitely assigned before" }
};

ReadBeforeWrite make1 (int v) { return ReadBeforeWrite(v); }
RefToUninitMemberNeverAssigned make2 (int x) { return RefToUninitMemberNeverAssigned(x); }
