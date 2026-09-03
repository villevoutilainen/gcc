// P4222's [[uninit]] attribute only applies to automatic (local,
// non-static) variables -- rejected on a namespace-scope variable,
// same restriction C++26's [[indeterminate]] already has.
// { dg-do compile { target c++11 } }

[[uninit]] int g; // { dg-error "on declaration other than automatic variable" }
