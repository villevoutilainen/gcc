// D4324, Increment S: check_narrowing's conveyor gate (typeck2.cc's
// check_narrowing itself) already covers every call site automatically
// -- confirmed here for the direct-list-initialization-of-a-fixed-
// underlying-type-enum call site (decl.cc/typeck.cc's
// is_direct_enum_init path), which previously had no dedicated test.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

enum E : int { A, B };

int f (long x) conveyor
{
  E e{x}; // { dg-error "narrowing conversion of .x. from .long int. to .int. not permitted in a conveyor function or predicate" }
  return e;
}
