// D4324: declaration-level contracts on callable-typed object
// declarations (see .claude/plans/stateless-jumping-shore.md) -- Rule
// 3 (revised): when a clause omits the binder list, its plain
// condition resolves names via ordinary unqualified lookup against
// whatever real names the declarator's own parameter list happens to
// provide, regardless of whether that's all, some, or none of them.
// Referencing a parameter that has no name is an ordinary "not
// declared" lookup error -- not a dedicated "mixed naming" diagnostic.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fsyntax-only" }

#include <contracts>

// The declarator names only its first parameter; a plain condition
// referencing just that one is fine.
void f (void (*) (int x, int) pre<> (x > 0));

// Same declarator shape, but the condition also references the
// unnamed second parameter: an ordinary undeclared-identifier error,
// not a dedicated diagnostic.
void g (void (*) (int x, int) pre<> (x > 0 && y > 0)); // { dg-error "'y' was not declared in this scope" }

// All parameters named: the plain-condition shortcut, unaffected.
void h (void (*) (int x, int y) pre<> (x > y));

int main () { return 0; }
