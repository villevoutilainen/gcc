// P4222 Initialization profile: assigning a [[ref_to_uninit]]-flavored
// function's return value into an UNMARKED destination is an error --
// same bidirectional flavor-mismatch rule as an ordinary assignment
// (d4324-profiles-init-ref-to-uninit-assign-unmarked-dest-bad.C), now
// also covering a call's own return value via ip_arg_uninit_
// flavored_p's GIMPLE_CALL-def recursion.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

typedef __SIZE_TYPE__ size_t;

[[ref_to_uninit]] void* my_malloc (size_t n);

int main ()
{
  void* p = my_malloc (4); // { dg-error "assigning a pointer marked \[^\n\]*ref_to_uninit\[^\n\]* into a pointer not marked" }
}
