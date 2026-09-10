// P4222 Initialization profile: constructor-member (this->field)
// counterpart of d4324-profiles-uninit-member-addr-into-flavored-
// pointer-ok.C -- 'p = &a;''s own single-hop destination-flavor check
// (see that file's own comment) applies identically here.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct X
{
  int a [[uninit]];
  X ()
  {
    int* p [[ref_to_uninit]] = &a;
    int* q [[ref_to_uninit]] = p;
    (void) q;
  }
};
