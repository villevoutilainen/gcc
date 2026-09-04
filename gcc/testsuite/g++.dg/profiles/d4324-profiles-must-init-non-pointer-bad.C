// P4222 Initialization profile: [[must_init]] (and [[ref_to_uninit]],
// same handler shape) is now allowed on REFERENCE_TYPE parameters, not
// just POINTER_TYPE ones (see d4324-profiles-now-init-in-place-ok.C),
// but that relaxation is scoped to pointer-or-reference, not "any
// type" -- a plain by-value parameter is still rejected.
// { dg-do compile { target c++11 } }

void f (int x [[must_init]]); // { dg-error "only supported on a pointer- or reference-typed parameter" }
