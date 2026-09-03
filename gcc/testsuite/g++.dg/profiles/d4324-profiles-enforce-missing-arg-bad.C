// P3589, Increment 1: profiles::enforce (and suppress) require an
// argument -- same shape of diagnostic omp::directive already gives
// for the identical mistake.
// { dg-do compile { target c++11 } }

[[profiles::enforce]]; // { dg-error "attribute requires argument" }
