// D4324: declaration-level contracts on callable-typed object
// declarations (see .claude/plans/stateless-jumping-shore.md) --
// redeclaration consistency, modeled on the existing rule for ordinary
// function contracts (check_redecl_contract): a later declaration may
// omit the clause (inheriting the first declaration's), must match
// exactly if both specify one, and may never add a clause the first
// declaration didn't have.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fsyntax-only" }

#include <contracts>

// An extern forward declaration's clause is inherited by a later,
// clause-omitting real declaration.
extern void (*fp1) (int x, int y) pre<> (x > y);
void (*fp1) (int x, int y) = nullptr;

// A later declaration repeating the exact same clause is fine.
extern void (*fp2) (int x, int y) pre<> (x > y);
void (*fp2) (int x, int y) pre<> (x > y) = nullptr;

// A later declaration adding a clause the first one didn't have.
extern void (*fp3) (int x, int y);
void (*fp3) (int x, int y) pre<> (x > y) = nullptr; // { dg-error "declaration adds a contract specifier" }

// A later declaration with a structurally different clause.
extern void (*fp4) (int x, int y) pre<> (x > y);
void (*fp4) (int x, int y) pre<> (x < y) = nullptr; // { dg-error "mismatched contract condition" }

int main () { return 0; }
