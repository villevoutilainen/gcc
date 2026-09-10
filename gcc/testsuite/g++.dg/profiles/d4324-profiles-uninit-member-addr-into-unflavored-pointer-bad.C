// P4222 Initialization profile: negative control for
// d4324-profiles-uninit-member-addr-into-flavored-pointer-ok.C -- the
// same 'p = &x.a; q = p;' shape, but q is NOT declared
// [[ref_to_uninit]], so this must stay flagged on both counts: the
// escape check on 'p = &x.a;' itself (p's only use, the plain copy into
// an unflavored q, isn't a safe hop), and the pre-existing flavor-
// consistency mismatch on 'q = p;'.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct X { int a; int b; };

void f ()
{
  X x [[uninit]];
  int* p [[ref_to_uninit]] = &x.a; // { dg-error "before it is provably initialized" }
  int* q = p; // { dg-error "assigning a pointer marked" }
  (void) q;
}
