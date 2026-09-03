// P4222 Initialization profile, Phase 3: [[ref_to_uninit]] flavor must
// match the pointee's [[uninit]] status in both directions.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void f ()
{
  int x1 = 7;
  int* p1 [[ref_to_uninit]] = &x1; // { dg-error "but .x1. is not marked" }

  [[uninit]] int x2;
  int* p2 = &x2; // { dg-error "points to .x2., which is marked" }

  (void) p1;
  (void) p2;
}
