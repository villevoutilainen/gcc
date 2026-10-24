// D4324: a conveyor member function declared in its class and defined
// outside it.  For a static member the out-of-class definition was
// first taken for a non-static one, given an implicit
// is_object_address (this) precondition, and then lost 'this' on
// matching the static declaration -- leaving a precondition the
// declaration doesn't have ("declaration adds contracts") on a
// parameter that no longer existed, which crashed.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct T
{
  static int plain (int x);
  static int s0 () conveyor;
  static int s1 (int x) conveyor;
  static int sp (int *p) conveyor;
  static int sr (const int &r) conveyor;
  int m0 () const conveyor;
  int mp (int *p) const conveyor;
};

int T::plain (int x) { return x; }
int T::s0 () conveyor { return 1; }
int T::s1 (int x) conveyor { return x; }
int T::sp (int *p) conveyor { return p != nullptr; }
int T::sr (const int &r) conveyor { return r; }
int T::m0 () const conveyor { return 1; }
int T::mp (int *p) const conveyor { return p != nullptr; }

int f (int x) conveyor;
int f (int x) conveyor { return x; }
