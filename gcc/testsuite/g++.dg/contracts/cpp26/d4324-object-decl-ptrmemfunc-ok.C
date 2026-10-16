// D4324: pointer-to-member-function declarations are now accepted by
// the declaration-level-contracts-on-callables eligibility gate
// (maybe_attach_object_contract_specifiers, decl.cc), which used to
// reject every such declaration with "contract specifier is only
// valid on a callable-typed declaration" -- confirms acceptance for a
// plain, const-qualified, lvalue-ref-qualified and rvalue-ref-
// qualified pointer-to-member-function, both as a variable and as a
// parameter, and confirms a ref-qualifier has no effect on 'this'
// itself within the condition (it only constrains what object value
// category the pointer can be invoked on, not the pointee's own body
// semantics, exactly like an ordinary ref-qualified member function's
// own 'this'): a const-qualified pointer gets a 'const C*' 'this',
// rejecting an attempted mutation through it.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fsyntax-only" }

#include <contracts>

struct C {
  int field;
  int member (int x) { return field + x; }
  int cmember (int x) const { return field + x; }
  int lmember (int x) &  { return field + x; }
  int rmember (int x) && { return field + x; }
};

int (C::*mfp)(int x) pre<> (this != nullptr && x >= 0);
int (C::*cmfp)(int x) const pre<> (this->field >= 0);
int (C::*lmfp)(int x) &  pre<> (this->field >= 0);
int (C::*rmfp)(int x) && pre<> (this->field >= 0);

void f (int (C::*p)(int x) pre<> (this != nullptr && x >= 0));

// A const-qualified pointer's 'this' is 'const C*' -- mutating through
// it is rejected, just as it would be inside cmember's own body.
int (C::*cmfp_bad)(int x) const pre<> ((this->field = 1) >= 0); // { dg-error "assignment of member" }
