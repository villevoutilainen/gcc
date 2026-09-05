// P3589: the identical -fprofiles-enforced=std::init flag as
// d4324-profiles-command-line-enforced-bad.C, but on code that
// already satisfies the profile -- confirms command-line enforcement
// applies the same real checking an in-source
// '[[profiles::enforce]]' would, not just a blanket rejection.
// { dg-do compile { target c++11 } }
// { dg-options "-fprofiles-enforced=std::init" }

void f ()
{
  int x [[uninit]];
  x = 1;
  (void) x;
}
