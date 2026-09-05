// P4222 Initialization profile: the initializer-only counterpart of
// d4324-profiles-init-ref-to-uninit-assign-unmarked-dest-bad.C -- a
// [[ref_to_uninit]]-flavored source assigned into an unmarked
// destination via its own declaration's initializer, not a later
// assignment, is equally flagged (same GIMPLE_ASSIGN shape either way).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void* src [[ref_to_uninit]] = nullptr;

int main ()
{
  int* dst = (int*) src; // { dg-error "assigning a pointer marked \[^\n\]*ref_to_uninit\[^\n\]* into a pointer not marked" }
  (void) dst;
}
