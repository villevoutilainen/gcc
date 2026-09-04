// P4222 Initialization profile, Phase 4b (S5.1-S5.3): a constructor
// covering every member -- via the member-initializer-list, an NSDMI,
// or straight-line body code for a member marked [[uninit]]/
// [[ref_to_uninit]] to exempt it from the first two -- is accepted.
//
// Each struct is also actually instantiated below (use_them), not
// just defined: an inline constructor that's declared but never
// called never gets GIMPLE-compiled at all, so this file's own
// per-member checker (ip_check_constructor_member, init-profile-
// gimple.cc) previously never even ran on any of these, and this test
// passed without ever exercising the thing it claims to verify.
// Found 2026-09-04: OkUninitMember's own m2 was never actually
// written anywhere -- an invalid example that only "passed" because
// it was never instantiated. [[uninit]] exempts a member from the
// member-initializer-list/NSDMI requirement specifically (S5.1); it
// does NOT exempt it from ever needing initialization by the time the
// object is exposed to callers -- that's still enforced via the same
// CFG-dominance-based DAA an address-taken local gets, matching
// member-body-daa-ok.C's own "paper's own flagship example" (a
// [[uninit]] member assigned via straight-line body code) and
// member-body-daa-bad.C's own NeverInit (one left untouched entirely,
// correctly rejected). Fixed by giving OkUninitMember's own m2 that
// same straight-line write, instead of loosening the checker itself.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct OkExplicit {
  int m1;
  int m2;
  OkExplicit (int x) : m1{x}, m2{x} {}
};

struct OkNSDMI {
  int m1 = 7;
  int m2;
  OkNSDMI (int x) : m2{x} {}
};

struct OkUninitMember {
  int m1;
  int m2 [[uninit]];
  OkUninitMember (int x) : m1{x}
  {
    m2 = x;
  }
};

struct OkRefToUninitMember {
  int m1;
  int* m2 [[ref_to_uninit]];
  OkRefToUninitMember (int x, int* p) : m1{x}, m2{p} {}
};

void use_them ()
{
  OkExplicit e (1);
  OkNSDMI n (2);
  OkUninitMember u (3);
  int scratch = 0;
  OkRefToUninitMember r (4, &scratch);
  (void) e;
  (void) n;
  (void) u;
  (void) r;
}
