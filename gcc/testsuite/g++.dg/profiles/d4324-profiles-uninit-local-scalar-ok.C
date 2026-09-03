// P4222 Initialization profile, Increment 1 (local scalars only): a
// local scalar variable explicitly marked [[uninit]] is accepted even
// though it has no initializer.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void f ()
{
  [[uninit]] int x;
  [[uninit]] int *p;
  (void) x;
  (void) p;
}
