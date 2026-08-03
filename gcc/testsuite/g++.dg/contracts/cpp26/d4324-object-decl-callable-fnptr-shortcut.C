// D4324: declaration-level contracts on callable-typed object
// declarations (see .claude/plans/stateless-jumping-shore.md).  A
// pre<>/post<> clause attached to a function-pointer-shaped object
// declaration -- a top-level variable, a static data member, or a
// function parameter -- when the declarator names all of its own
// parameters, reuses those real names directly (no binder list
// needed): this is the grammar and semantic-attach slice, using
// build_contract_check dispatch/enforcement is separate, later work.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fsyntax-only" }

#include <contracts>

struct ctrl { void operator() (const std::contracts::assertion_context&) const {} };
inline constexpr ctrl ctrl_v{};

// Top-level variable of function-pointer type.
void (*fp1) (int x, int y) pre<ctrl_v> (x > y);
void (*fp2) (int x, int y) pre<> (x > y);

// A void-returning function pointer's post<> names only parameters.
// (P2900 constification: a value parameter used in post<> must itself
// be declared const, exactly as for a real function's own post.)
void (*fp3) (const int x) post<> (x >= 0);

// A non-void-returning function pointer's post<> names the result too.
int (*fp4) (const int x) post<> (r: r >= x);

struct S
{
  // Static data member of function-pointer type.
  static void (*mfp) (int x, int y) pre<> (x > y);
};

// A function parameter of function-pointer type.
void f (void (*) (int x, int y) pre<> (x > y));
void g (void (*)(const int x, const int y) post<>(x > y));

int main () { return 0; }
