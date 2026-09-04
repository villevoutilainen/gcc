// P4222 Initialization profile, Phase 4b (S5.1-S5.3): a constructor
// covering every member -- via the member-initializer-list, an NSDMI,
// or (for a member marked literally [[uninit]]) not covering it at
// all, since the entire point of [[uninit]] is that no promise is
// made even by the constructor -- is accepted. A [[ref_to_uninit]]
// member is different: its own pointer VALUE still needs to be
// assigned somewhere (the member-initializer-list here), only its
// pointee is exempted.
//
// Each struct is also actually instantiated below (use_them), not
// just defined: an inline constructor that's declared but never
// called never gets GIMPLE-compiled at all, so this file's own
// per-member checker (ip_check_constructor_member, init-profile-
// gimple.cc) previously never even ran on any of these, and this test
// passed without ever exercising the thing it claims to verify. See
// member-body-daa-ok.C/-bad.C for the same distinction exercised via
// real CFG-dominance-based DAA (conditionally-assigned, read-before-
// write, and never-assigned-at-all member shapes).
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
  OkUninitMember (int x) : m1{x} {}
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
