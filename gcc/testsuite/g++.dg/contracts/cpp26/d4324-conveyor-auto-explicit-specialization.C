// D4324: an explicit specialization of a 'conveyor(auto)' function template
// is a function of its own: 'conveyor' or not, as an explicit
// specialization of a constexpr function template may or may not be
// constexpr.  'conveyor(auto)' itself is for templates only.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct S { int v; int data () const conveyor { return v; } };
struct T { int v; int data () const { return v; } };

template<class X> int id (const X& x) conveyor(auto) { return int (x); }
template<> int id<S> (const S& s) conveyor { return s.data (); }
template<> int id<T> (const T& t) { return t.data (); }
template<> int id<long> (const long& l) conveyor(auto) { return int (l); } // { dg-error "may only be used in a template" }

int use (const S& s, const T& t, int i) conveyor
{
  return id (s) + id (i) + id (t); // { dg-error "not declared .conveyor." }
}
