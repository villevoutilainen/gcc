// D4324, Increment T: confirms the existing, already-working
// exception-specification mismatch check for an explicit instantiation
// still fires correctly, with no interference from the new conveyor
// check added alongside it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

template <class T> T f (T x) noexcept { return x; }
template int f<int> (int) noexcept (false); // { dg-error "exception specification .noexcept \\(false\\). in explicit instantiation does not match the instantiated one .noexcept." }
