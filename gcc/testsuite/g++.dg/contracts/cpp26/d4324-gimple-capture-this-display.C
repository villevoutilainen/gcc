// D4324: the GIMPLE engine names a lambda's captured 'this' in a
// diagnostic -- here an argument of a call shown as another call's
// argument -- without crashing on the name having no type.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

struct T;
T *id (T &r) conveyor;
int g (T *p) conveyor pre<std::contracts::conveyor_assert_v> (std::is_object_address (p));

struct T
{
  int f ()
  {
    return [this] { return g (id (*this)); } (); // { dg-error "cannot prove 'is_object_address' for '\\* this'" }
    // { dg-error "cannot prove 'is_object_address' for 'id\\(\\(\\* this\\)\\)'" "" { target *-*-* } .-1 }
  }
};

int h (T &t) { return t.f (); }
