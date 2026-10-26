// D4324: the counterpart of d4324-conveyor-out-of-class-definition-
// inherits-ok.C -- an out-of-class definition that omits its contracts
// inherits the first declaration's, so the implicit precondition of a
// conveyor function's reference parameter, and a user-written proven one,
// still bind callers that can't show their argument is an object.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-conveyor-proofs" }

#include <contracts>

struct util
{
  static int implicit_only (int& ref) conveyor;

  static int proven (int& ref) conveyor
    pre<std::contracts::conveyor_assert_v>
      (std::is_object_address (__builtin_addressof (ref)));
};

int util::implicit_only (int& ref) conveyor
{ return ref; }

int util::proven (int& ref) conveyor
{ return ref; }

int f (int *p)
{
  return util::implicit_only (*p) // { dg-error "cannot prove .is_object_address." }
	 + util::proven (*p);	  // { dg-error "cannot prove .is_object_address." }
}
