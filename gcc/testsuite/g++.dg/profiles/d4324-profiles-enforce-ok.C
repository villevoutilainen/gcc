// P3589, Increment 1: [[profiles::enforce(name)]] on a registered
// profile, correctly placed (global scope, before any non-empty
// declaration), including P3589's own explicit allowance for repeating
// an identical profile-enforcement attribute with no effect from the
// repetition. "std::init" is the one profile registered so far
// (profiles.cc's own minimal registry -- the informal name P4222's
// initialization profile already goes by).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];
[[profiles::enforce(std::init)]];

int x;
