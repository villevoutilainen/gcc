// D4324: declaration-level contracts on callable-typed object
// declarations (see .claude/plans/stateless-jumping-shore.md) -- the
// comma-separated binder-list grammar, generalizing the existing
// single postcondition result-name binder (post(r: cond)) to a list
// usable by both pre and post, for a callable whose own declarator
// doesn't already name all of its parameters.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fsyntax-only" }

#include <contracts>

// pre: a binder list naming both (otherwise unnamed) parameters.
void (*fp1) (int, int) pre<> (x, y: x > y);

// post, void return: the binder list is exactly the parameter names,
// no result (there is nothing to name).
void (*fp2) (int, int) post<> (x, y: x > y);

// post, non-void return: the binder list is the result first, then
// the parameters, in order.
int (*fp3) (int, int) post<> (r, x, y: r == x + y);

// pre: a single-name binder list unambiguously names the one
// parameter (arity 1) -- 'pre' never had any existing binder grammar,
// so there is no backward-compatible "result name" meaning to collide
// with here (contrast with post's single-name form below).
void (*fp4) (int) pre<> (x: x > 0);

// post's own single-name binder keeps meaning "the result", exactly as
// it always has, regardless of arity -- required for backward
// compatibility with existing single-parameter, non-void functions
// using post(r: cond) (see vaargs.C).  This is *not* a way to name a
// single otherwise-unnamed parameter with no result; that combination
// isn't expressible via post's binder list in this increment.
int (*fp5) (int) post<> (r: r > 0);

// A function parameter of function-pointer type, from the plan's own
// worked example.
void f (void (*) (int, int) post<> (r, x, y: r == x + y));

// Wrong binder count against the callable's own arity is rejected.
// (The condition itself doesn't reference any of the mis-sized binder
// list's names, so this exercises only the arity check, without also
// tripping the further "not declared" errors that unresolved binder
// names would separately cause.)
void (*bad1) (int, int) pre<> (x, y, z: true); // { dg-error "wrong number of names in contract binder list" }

int main () { return 0; }
