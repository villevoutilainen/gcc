// P4222 Initialization profile: constructor-member (this->field)
// counterpart of d4324-profiles-uninit-member-addr-into-flavored-
// pointer-ok.C -- ip_scan_member_addr_uses had the identical gap as its
// local-aggregate sibling and got the identical fix.
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
