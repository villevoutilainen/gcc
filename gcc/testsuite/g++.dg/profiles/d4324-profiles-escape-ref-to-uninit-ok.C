// P4222 Initialization profile: std::escape_ref_to_uninit() -- the
// pointer-typed sibling of escape_uninit() -- lets a [[ref_to_uninit]]-
// flavored pointer be passed to an arbitrary, unannotated pointer-
// taking function (here, fill_somehow, standing in for e.g. the real,
// unannotated std::uninitialized_fill) without tripping the checker's
// call-argument flavor-consistency rejection. The object is then
// proven initialized afterward via now_init_in_place(), through the
// same underlying variable, exactly as if the escape had never
// happened -- mirroring d4324-profiles-escape-uninit-ok.C's own
// reference-typed shape.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
#include <utility>

void fill_somehow (int* p);

int use_it ()
{
  [[uninit]] int x;
  int* p [[ref_to_uninit]] = &x;
  fill_somehow (std::escape_ref_to_uninit (p));
  std::now_init_in_place (x);
  return x;
}
