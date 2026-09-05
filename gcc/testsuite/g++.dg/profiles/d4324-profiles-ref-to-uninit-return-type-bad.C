// P4222 Initialization profile: [[ref_to_uninit]] on a FUNCTION_DECL
// requires its return type be a pointer or reference, mirroring the
// existing PARM_DECL/VAR_DECL/FIELD_DECL restriction -- confirmed both
// of C++'s two declaration-level attribute positions reach the same
// validation.
// { dg-do compile { target c++11 } }

[[ref_to_uninit]] int not_a_pointer (); // { dg-error "only supported on a function returning a pointer or reference" }
int not_a_pointer2 [[ref_to_uninit]] (); // { dg-error "only supported on a function returning a pointer or reference" }
