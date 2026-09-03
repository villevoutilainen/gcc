// P3589, Increment 1: profiles::enforce must precede any
// non-empty-declaration in the translation unit.
// { dg-do compile { target c++11 } }

int x;
[[profiles::enforce(std::init)]]; // { dg-error "must appear before any declaration" }
