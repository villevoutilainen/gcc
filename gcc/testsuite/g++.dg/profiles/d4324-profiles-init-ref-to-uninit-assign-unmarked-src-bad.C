// P4222 Initialization profile: the reverse direction of the previous
// test -- assigning an ordinary, unflavored pointer into a
// [[ref_to_uninit]]-marked destination is equally an error, matching
// ip_check_call_flavor_consistency's own existing bidirectional rule
// for call arguments (d4324-profiles-call-flavor-mismatch-bad.C).
// Uses a plain unmarked pointer VARIABLE as the source, not a direct
// '&non_uninit_var' (which would instead trip decl.cc's own separate,
// pre-existing "a [[ref_to_uninit]] pointer's address-of target must
// itself be [[uninit]]" declaration-time check -- see d4324-profiles-
// ref-to-uninit-decl-bad.C -- an unrelated rule this test is not
// about).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int* ordinary = nullptr;

int main ()
{
  int* dst [[ref_to_uninit]] = ordinary; // { dg-error "assigning a pointer not marked \[^\n\]*ref_to_uninit\[^\n\]* into a pointer marked" }
  dst = ordinary; // { dg-error "assigning a pointer not marked \[^\n\]*ref_to_uninit\[^\n\]* into a pointer marked" }
}
