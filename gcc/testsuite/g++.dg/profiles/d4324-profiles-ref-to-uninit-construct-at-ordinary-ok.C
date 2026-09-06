// P4222 Initialization profile: std::construct_at's own first argument
// is exempt from the ordinary bidirectional flavor-consistency check
// in EITHER direction -- an ordinary, unflavored pointer (the common,
// real-world case: fresh storage from operator new, or re-constructing
// over existing storage) passed to it must not be rejected just
// because construct_at's own (unmodified, real) declaration has no
// [[ref_to_uninit]] of its own to match.
// { dg-do compile { target c++20 } }

[[profiles::enforce(std::init)]];
[[profiles::exempt(std::init, angle_header: "memory")]];

#include <memory>

int main ()
{
  int storage = 0;
  int* p = &storage;
  int* q = std::construct_at (p, 5);
  return *q;
}
