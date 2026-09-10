// P4222 Initialization profile: negative control, constructor-member
// counterpart of d4324-profiles-uninit-member-addr-into-unflavored-
// pointer-bad.C.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct X
{
  int a [[uninit]];
  X ()
  {
    int* p [[ref_to_uninit]] = &a; // { dg-error "before it is provably initialized" }
    int* q = p; // { dg-error "assigning a pointer marked" }
    (void) q;
  }
};
