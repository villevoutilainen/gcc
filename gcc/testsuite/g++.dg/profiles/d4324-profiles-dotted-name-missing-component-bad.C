// P3589, Increment 1: a trailing "::" with no following identifier in
// a profile-name is a syntax error, not silently accepted.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::)]]; // { dg-error "expected profile name component" }
