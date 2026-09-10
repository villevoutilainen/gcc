// P4222 Initialization profile: negative control, constructor-member
// counterpart of d4324-profiles-uninit-member-addr-into-unflavored-
// pointer-bad.C -- see that file's own comment: 'p = &a;' stays clean
// (p is declared [[ref_to_uninit]], exactly flavor-consistent with a
// still-[[uninit]] pointee, regardless of p's later use), the sole
// violation is 'q = p;' silently dropping the flavor into unflavored q.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct X
{
  int a [[uninit]];
  X ()
  {
    int* p [[ref_to_uninit]] = &a;
    int* q = p; // { dg-error "assigning a pointer marked" }
    (void) q;
  }
};
