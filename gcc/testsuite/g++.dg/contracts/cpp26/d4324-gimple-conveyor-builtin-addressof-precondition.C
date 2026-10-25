// D4324: d4324-conveyor-builtin-addressof-precondition.C under the GIMPLE
// engine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
using std::contracts::never_proven_conveyor_v;

struct R { int v; int begin () const conveyor { return v; } };

template<class T> constexpr auto amp (T &t)
  pre<never_proven_conveyor_v> (std::is_object_address (&t))
{ return t.begin (); }

template<class T> constexpr auto bao (T &t)
  pre<never_proven_conveyor_v> (std::is_object_address (__builtin_addressof (t)))
{ return t.begin (); }

template<class T> T &dv ();
template<class T> using it_a = decltype (amp (dv<T> ()));
template<class T> using it_b = decltype (bao (dv<T> ()));
it_a<R> xa;
it_b<R> xb;
int use (R &r) { return amp (r) + bao (r); }
