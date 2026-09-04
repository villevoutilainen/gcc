// P3446R0/P4296R0 Invalidation profile: with no
// profiles::enforce(std::invalidation) in the translation unit, none
// of the Negative Baseline's checks apply -- ordinary C++ rules
// apply.
// { dg-do compile { target c++11 } }

struct S { ~S() {} };

int f (int *p)
{
  delete p;
  int x = *p;
  S *sp = new S ();
  sp->~S ();
  float *fp = reinterpret_cast<float *> (p);
  (void) fp;
  return x;
}
