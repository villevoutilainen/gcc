// P4222 Initialization profile, Phase 3: the definition-side
// counterpart of d4324-profiles-must-init-dominance-{ok,bad}.C's own
// caller-side trust -- a function declaring [[must_init]] on a
// parameter must itself actually write through that parameter's
// pointee before every ordinary return.  Before this check existed,
// f's empty body let a caller's genuinely-uninitialized read compile
// clean (https://godbolt.org/z/W3aP983bW).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void f (int* p [[must_init]]) // { dg-error "function may return without" }
{
}
