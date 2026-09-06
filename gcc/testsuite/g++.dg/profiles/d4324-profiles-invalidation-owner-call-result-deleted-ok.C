// P3446R0/P4296R0 Invalidation profile: same as
// d4324-profiles-invalidation-owner-call-result-never-consumed-bad.C,
// but the result is properly deleted (and captured into an
// [[owner]]-marked local, avoiding the flavor mismatch too).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

[[owner]] int* make ();

void f ()
{
  [[owner]] int *p = make ();
  delete p;
}
