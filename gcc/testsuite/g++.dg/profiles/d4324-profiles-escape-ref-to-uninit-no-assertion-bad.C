// P4222 Initialization profile: std::escape_ref_to_uninit() only
// neutralizes the call-argument flavor-consistency check -- it does
// not, unlike now_init()/now_init_in_place(), also assert that the
// pointee is initialized. A direct read of x after escape_ref_to_
// uninit(p) alone, with no following now_init()/now_init_in_place(),
// is still correctly rejected -- mirroring d4324-profiles-escape-
// uninit-no-assertion-bad.C's own reference-typed shape.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
#include <utility>

void fill_somehow (int* p);

int use_it ()
{
  [[uninit]] int x;
  int* p [[ref_to_uninit]] = &x;
  fill_somehow (std::escape_ref_to_uninit (p));
  return x; // { dg-error "read before it is definitely assigned" }
}
