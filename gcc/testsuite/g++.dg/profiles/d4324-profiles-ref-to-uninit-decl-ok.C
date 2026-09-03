// P4222 Initialization profile, Phase 3: [[ref_to_uninit]] on a local
// pointer initialized directly from an [[uninit]] variable's address
// is accepted; an ordinary pointer initialized from an initialized
// variable's address is accepted too.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void f ()
{
  [[uninit]] int x3;
  int* p3 [[ref_to_uninit]] = &x3;

  int x4 = 7;
  int* p4 = &x4;

  (void) p3;
  (void) p4;
}
