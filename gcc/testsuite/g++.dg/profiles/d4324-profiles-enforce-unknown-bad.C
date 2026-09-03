// P3589, Increment 1: an unrecognized profile name is a hard error,
// not silently ignored -- profiles.cc's minimal registry only knows
// about "std::init" so far.
// { dg-do compile { target c++11 } }

[[profiles::enforce(bogus_profile)]]; // { dg-error "unknown profile" }
