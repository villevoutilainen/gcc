// P4222 Initialization profile: [[uninit]] is not a permanent property
// of a declaration -- an ordinary direct write (IE-1) cures it for
// every purpose, not just later reads: a subsequent unverified
// address-escape and an unverified pointer-flavor mismatch are both
// cured too, exactly as if the local had never been marked [[uninit]]
// at all.  Companion to d4324-profiles-uninit-address-taken-bad.C and
// d4324-profiles-ref-to-uninit-decl-bad.C, which show the uncured
// (still [[uninit]]) shapes of these same two checks.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void take_ptr (int *);

void f ()
{
  int x [[uninit]];
  x = 5;
  take_ptr (&x);
  int *p = &x;
  (void) p;
}
