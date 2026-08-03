// D4324: declaration-level contracts on callable-typed object
// declarations (see .claude/plans/stateless-jumping-shore.md) -- the
// anonymous-declaration case (an unnamed function parameter of
// callable type) is unaffected by anonymity, since both the control-
// object specifier and the binder list are entirely self-contained
// inside pre/post's own parens, and cp_build_parm_decl already accepts
// a NULL name.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fsyntax-only" }

#include <contracts>

// Anonymous parameter, binder list (declarator names none of its own
// parameters).
void f (void (*) (int, int) pre<> (x, y: x > y));

// Anonymous parameter, shortcut (declarator names all of its own
// parameters, no binder list needed).
void g (void (*) (int x, int y) pre<> (x > y));

int main () { return 0; }
