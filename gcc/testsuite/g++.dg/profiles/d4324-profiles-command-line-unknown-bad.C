// P3589: an unrecognized profile name given to -fprofiles-enforced=
// is a hard error, matching profiles::enforce's own identical
// diagnostic for the same situation -- reported without a source
// location, since there is none for a command-line argument.
// { dg-do compile { target c++11 } }
// { dg-options "-fprofiles-enforced=std::bogus" }
// { dg-error "unknown profile" "" { target *-*-* } 0 }

void f () {}
