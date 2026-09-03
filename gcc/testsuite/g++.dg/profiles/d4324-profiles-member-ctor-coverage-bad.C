// P4222 Initialization profile, Phase 4b (S5.1): a constructor that
// leaves a non-[[uninit]] member uncovered (no mem-initializer, no
// NSDMI) is rejected.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct BadMissing {
  int m1;
  int m2;
  BadMissing (int x) : m1{x} {} // { dg-error "does not initialize member" }
};
