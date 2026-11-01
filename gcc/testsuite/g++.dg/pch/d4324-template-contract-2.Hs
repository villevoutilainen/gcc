// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }
// D4324: a template defined in a PCH keeps its contracts when it is
// instantiated after the PCH is loaded.  The contract specifier table
// (and every other pointer-keyed contracts table) is rehashed then: the
// PCH moved its keys.
#include <contracts>

template <class T>
T &
tid (T &x) conveyor
  pre<std::contracts::never_proven_conveyor_v> (std::is_object_address (&x))
{ return x; }

template <class T>
T &
tid_strict (T &x) conveyor
  pre<std::contracts::conveyor_assert_v> (std::is_object_address (&x))
{ return x; }
