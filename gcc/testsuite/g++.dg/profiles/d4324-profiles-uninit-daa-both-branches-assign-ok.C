// P4222 Initialization profile, GIMPLE checker: real Definite
// Assignment Analysis -- a [[uninit]] local assigned on every arm of
// an if/else is accepted at the merge point.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int use (int);

void f (bool c)
{
  [[uninit]] int x;
  if (c)
    x = 1;
  else
    x = 2;
  use (x);
}
