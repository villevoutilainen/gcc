// P4222 Initialization profile: negative control for
// d4324-profiles-uninit-member-addr-into-flavored-pointer-ok.C -- the
// same 'p = &x.a; q = p;' shape, but q is NOT declared
// [[ref_to_uninit]]. 'p = &x.a;' itself stays clean either way -- p is
// declared [[ref_to_uninit]], so it's exactly flavor-consistent with
// pointing at still-[[uninit]] memory, full stop, regardless of what
// happens to p afterward (see the -ok.C counterpart's own comment).
// The real mistake here is entirely at 'q = p;', where a flavored
// pointer's flavor is silently dropped by copying it into an unflavored
// q -- that's what flavor-consistency (Assign-flavor-mismatch) alone
// catches.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct X { int a; int b; };

void f ()
{
  X x [[uninit]];
  int* p [[ref_to_uninit]] = &x.a;
  int* q = p; // { dg-error "assigning a pointer marked" }
  (void) q;
}
