// Negative control for d4324-profiles-must-init-through-traced-pointer-
// ok.C: p traces to an unrelated variable y, not x -- x must still be
// correctly reported as uninitialized, confirming the fix did not
// widen "p traces to &var" into curing every variable indiscriminately.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void f (int *p [[must_init]]);

int g ()
{
  [[uninit]] int x;
  int y = 5;
  int *p = &y;
  f (p); // { dg-error "must refer to \[^\n\]*uninit\[^\n\]* memory" }
  return x; // { dg-error "read before it is definitely assigned" }
}
