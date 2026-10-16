// D4324: declaration-level contracts on callable-typed object
// declarations -- the "divergent call" rejection (see
// d4324-object-decl-class-callable-divergent-bad.C) reproduced for a
// *field* instead of a variable, through the same field-access fix
// that resolves the contract's owner via fnptr_contract_owner
// (typeck.cc, semantics.cc).  A call that resolves through anything
// other than the operator() the declaration's contract was checked
// against is a hard compile-time error at that call site.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fsyntax-only" }

#include <contracts>

typedef int (*fnptr) (int, int);

int real_add (int a, int b) { return a + b; }

struct Incompatible {};

struct wrapper {
  int operator() (Incompatible, Incompatible) const { return 0; }
  operator fnptr () const { return &real_add; }
};

struct Holder {
  wrapper w pre<> (true);
};

int main ()
{
  Holder h{};
  h.w (1, 2); // { dg-error "call does not resolve through the .operator\\(\\). declared for .Holder::w., so its contract specifier does not apply to this call" }
  return 0;
}
