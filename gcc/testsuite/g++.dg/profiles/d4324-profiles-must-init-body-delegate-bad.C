// Companion to d4324-profiles-must-init-body-ok.C's delegate:
// delegating to a function whose corresponding parameter is NOT
// itself [[must_init]]-flavored does not count as an initializing
// event, so it does not cure the exit-dominance check.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void not_must_init (int* q [[ref_to_uninit]]);

void delegate_does_not_cure (int* p [[must_init]]) // { dg-error "function may return without" }
{
  not_must_init (p);
}
