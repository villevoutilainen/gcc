// D4324: a class template instantiated from a conveyor predicate is not
// part of the predicate.  W<int>'s member template names the non-conveyor
// r() in a parameter type; that was checked against the conveyor rules
// when the predicate happened to instantiate W<int> first, and accepted
// when anything else did (P4445 BUG-25).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>

template<class U, int N> struct A {};

template<class T> struct W
{
  static constexpr int r () { return 2; }	// not conveyor
  template<class U> void f (A<U, r ()>) const {}
  int get () const conveyor { return 1; }
};

void p (const W<int>& w) pre<std::contracts::conveyor_assert_v>(w.get () > 0) {}

int q () conveyor { W<long> w {}; return w.get (); }

// A call in the predicate itself is still checked.
template<class T> struct X
{
  static int r () { return 2; }
};
void s (const X<int>&) pre<std::contracts::conveyor_assert_v>(X<int>::r () > 0) {} // { dg-error "not declared 'conveyor'" }
