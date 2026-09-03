// P4222 Initialization profile, Phase 4b (S5.1-S5.3): a constructor
// covering every member -- via the member-initializer-list, an NSDMI,
// or an explicit [[uninit]]/[[ref_to_uninit]] exemption -- is
// accepted.
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
