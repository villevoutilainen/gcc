// P4222 Initialization profile, GIMPLE checker: a [[uninit]] local
// assigned before its only read is accepted.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int use (int);

void f ()
{
  [[uninit]] int x;
  x = 5;
  use (x);
}
