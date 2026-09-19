// Companion to d4324-profiles-must-init-body-ok.C's both_branches:
// writing through the [[must_init]] parameter's pointee on only ONE
// branch does not dominate every return path.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void one_branch_only (int* p [[must_init]], bool c) // { dg-error "function may return without" }
{
  if (c)
    *p = 1;
}
