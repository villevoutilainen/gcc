// D4324: a trust of a reference bound to a call's result ('Vec& lvalue =
// original', a conversion function; 'g ()') makes its object one again
// after a call given it may have ended it.  The gimple engine.  GIMPLE kept
// the copy of the pointer value the later call used ended.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple -O1" }

#include <contracts>

struct Base { int* b; int* e; long size () const conveyor { return e - b; } };
struct Vec : Base { int get_allocator () const; };
template<class T> struct MovableRef { T* p; operator T& () const { return *p; } };
Vec& g ();

long converted (MovableRef<Vec> original)
{
  Vec& lvalue = original;
  lvalue.get_allocator ();
  contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (&lvalue));
  return lvalue.size ();
}

long returned ()
{
  Vec& lvalue = g ();
  lvalue.get_allocator ();
  contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (&lvalue));
  return lvalue.size ();
}

long not_again ()
{
  Vec& lvalue = g ();
  contract_assert<std::contracts::never_proven_conveyor_v> (std::is_object_address (&lvalue));
  lvalue.get_allocator ();
  return lvalue.size (); // { dg-error "cannot prove" }
}
