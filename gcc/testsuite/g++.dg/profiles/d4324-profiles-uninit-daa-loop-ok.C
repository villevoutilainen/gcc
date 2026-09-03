// P4222 Initialization profile, GIMPLE checker: a [[uninit]] local
// assigned at the top of every loop iteration before its own read
// within that same iteration is accepted.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int use (int);

void f ()
{
  [[uninit]] int x;
  for (int i = 0; i < 3; ++i)
    {
      x = i;
      use (x);
    }
}
