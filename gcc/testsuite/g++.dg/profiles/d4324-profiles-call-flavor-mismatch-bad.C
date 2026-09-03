// P4222 Initialization profile, Phase 3: call-argument pointer-flavor
// consistency is checked independent of whether the variable involved
// is itself [[uninit]] -- passing an ordinary (initialized) pointer's
// address to a [[ref_to_uninit]] parameter is rejected.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void take_uninit (int* p [[ref_to_uninit]]);

void f (int y)
{
  take_uninit (&y); // { dg-error "must refer to \[^\n\]*memory, matching" }
}
