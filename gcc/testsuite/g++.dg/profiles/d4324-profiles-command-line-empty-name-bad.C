// P3589: a malformed -fprofiles-enforced= argument (here, a trailing
// comma leaving an empty profile name) is a hard error, not silently
// dropped.
// { dg-do compile { target c++11 } }
// { dg-options "-fprofiles-enforced=std::init," }
// { dg-error "empty profile name" "" { target *-*-* } 0 }

void f () {}
