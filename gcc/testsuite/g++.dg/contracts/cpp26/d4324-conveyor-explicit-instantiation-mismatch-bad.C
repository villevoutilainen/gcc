// D4324, Increment T: an explicit instantiation writing 'conveyor'
// where the template pattern itself is not conveyor must be rejected
// -- previously this was silently accepted, because the conveyor bit
// was applied to the DECL only after check_explicit_specialization had
// already read (and found unset) the bit inside determine_specialization's
// own mismatch guard.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

template <class T> T f (T x) { return x; }
template int f<int> (int) conveyor; // { dg-error ".conveyor. specified in explicit instantiation does not match the instantiated declaration" }
