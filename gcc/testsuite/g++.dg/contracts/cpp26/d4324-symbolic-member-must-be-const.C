// D4324: a non-static member function declared 'symbolic' is a predicate
// about its object and never changes it: it must be 'const', with no
// other qualifier.  An overload differing in qualification would be a
// different predicate, with facts of its own, so a 'const' member
// function's contract and a non-'const' one's would name different
// predicates.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct S
{
  bool a () const symbolic;
  bool b (S*) const symbolic;
  static bool c (S*) symbolic;

  bool d () symbolic; // { dg-error "must be 'const', with no other qualifier" }
  bool e () const volatile symbolic; // { dg-error "must be 'const', with no other qualifier" }
  bool f () const & symbolic; // { dg-error "must be 'const', with no other qualifier" }
  bool g () const && symbolic; // { dg-error "must be 'const', with no other qualifier" }
};

bool free_predicate (S*) symbolic;
