// P4222 Initialization profile: std::construct_at(&x, ...) on a local
// aggregate cures the whole-object address-escape check directly (it's
// a call whose first argument traces to &x, the ordinary IE-4 shape --
// unlike d4324-profiles-uninit-aggregate-per-field-cure-bad.C's
// per-field-store shape, this needs no special "every field covered"
// reasoning at all). Regression test for the exact shape confirmed
// working during that fix's own investigation.
// { dg-do compile { target c++20 } }

[[profiles::enforce(std::init)]];
[[profiles::exempt(std::init, angle_header: "memory")]];

#include <memory>

struct X { int y; };

void other (X* p);

void f ()
{
  X x [[uninit]];
  std::construct_at (&x, 42);
  other (&x);
}
