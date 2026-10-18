// D4324: a postcondition 'is_object_address (&r)' on a reference result R
// establishes the call result's address for the GIMPLE engine, as it
// does for the AST engine: '&r' arrives as a conversion of R itself.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs-gimple" }

#include <contracts>

struct A { int v; };

A &get (A &a) conveyor
  post<std::contracts::conveyor_assert_v> (r: std::is_object_address (&r))
{ return a; }

template <class T>
T &&mv (T &&t) conveyor
  post<std::contracts::conveyor_assert_v> (r: std::is_object_address (&r))
{ return static_cast<T &&> (t); }

int use (A &a) conveyor { return a.v; }

int direct (A &a) conveyor { return use (get (a)); }
int chained (A &a) conveyor { return use (mv (get (a))); }

A &unproven (A &a) conveyor { return a; }
int bad (A &a) conveyor { return use (unproven (a)); } // { dg-error "cannot prove .is_object_address." }

int main () { A a{1}; return direct (a) + chained (a) + bad (a); }
