// P4222 Initialization profile: ip_check_assign_flavor_consistency
// covers a declaration's own initializer with no special-casing at
// all, since 'T* q = p;' written as an initializer and 'q = p;'
// written as a later, separate assignment produce the identical
// GIMPLE_ASSIGN statement shape (confirmed via -fdump-tree-gimple).
// This test isolates the initializer form alone.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void* src [[ref_to_uninit]] = nullptr;

int main ()
{
  int* dst [[ref_to_uninit]] = (int*) src;
  (void) dst;
}
