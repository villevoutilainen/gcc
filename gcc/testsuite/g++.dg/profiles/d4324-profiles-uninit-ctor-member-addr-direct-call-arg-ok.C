// P4222 Initialization profile: '&this->field' passed directly as a
// call argument (no intermediate named variable at all) is never
// inlined at the point of use by GCC's own gimplifier -- unlike the
// equivalent local-aggregate call ('consume (&x.a);', which inlines the
// ADDR_EXPR directly) or 'this->field' assigned to a real named
// variable (which also inlines directly), this shape always lowers to
// '_1 = &this->field; consume (_1);' (confirmed via -fdump-tree-gimple)
// -- an anonymous SSA temporary with no source-level flavor of its own
// to check, so the escape checker has to look at the temporary's own
// single use instead. This covers the plain [[ref_to_uninit]] ("neutral":
// no diagnostic, and not an initializing event either) shape of that
// same underlying wrinkle; the [[must_init]] shape is already covered by
// MustInitCall in d4324-profiles-member-body-daa-ok.C, and the
// unflavored-callee negative control by
// d4324-profiles-member-ctor-address-escape-bad.C.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void consume (int* p [[ref_to_uninit]]);

struct RefToUninitArg
{
  int a [[uninit]];
  RefToUninitArg () { consume (&a); }
};
