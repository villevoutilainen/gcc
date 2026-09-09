// P4222 Initialization profile: std::construct_at(&x, ...) (IE-4)
// cures x for every purpose exactly like a plain write does (see
// d4324-profiles-uninit-cured-by-write-ok.C) -- not just for a later
// plain read, which construct_at already handled before this fix.
// { dg-do compile { target c++20 } }

[[profiles::enforce(std::init)]];
[[profiles::exempt(std::init, angle_header: "memory")]];

#include <memory>

void take_ptr (int *);

void f ()
{
  int x [[uninit]];
  std::construct_at (&x, 5);
  take_ptr (&x);
  int *p = &x;
  (void) p;
}
