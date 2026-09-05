// P4222 Initialization profile: [[ref_to_uninit]] on a function-pointer
// PARAMETER attaches without error (the attribute handler only checks
// the parameter's own type is POINTER_TYPE/REFERENCE_TYPE, not that
// the pointee is a data type rather than a function type) -- a
// harmless no-op for this profile, deliberately left unvalidated
// further: a function pointer has no "pointee memory" that could be
// initialized or not in the sense this profile reasons about.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

typedef __SIZE_TYPE__ size_t;

void* malloc_higher_order (void (*othermalloc [[ref_to_uninit]]) (size_t));
