// P4222 Initialization profile: flavor-consistency's COMPONENT_REF
// rule must gate on whether the FIELD is subject to uninit-tracking at
// all (the field itself individually marked [[uninit]], OR the whole
// containing variable marked [[uninit]]), not on whether the field
// carries the attribute directly -- a field of a whole-[[uninit]]
// aggregate never carries [[uninit]] itself (only the local variable
// does), so gating on the field's own attribute alone wrongly rejected
// this exact, legitimate pattern as a flavor mismatch before this fix
// (found while investigating why this case double-diagnosed
// differently from the scalar one).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct X { int a; int b; };

void f ()
{
  X x [[uninit]];
  int* p [[ref_to_uninit]] = &x.a;
  (void) p;
}
