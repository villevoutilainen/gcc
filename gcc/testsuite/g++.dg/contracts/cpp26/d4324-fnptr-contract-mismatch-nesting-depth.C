// D4324: -Wfunction-pointer-contract-mismatch false positive, fixed.
// contracts_equal_p (used only by this warning, never by the ordinary
// redeclaration-matching mismatched_contracts_p) compares two
// conditions via cp_tree_equal's PARM_DECL case, which -- when
// comparing_contracts is set -- treats two PARM_DECLs as "the same
// parameter" via DECL_PARM_LEVEL/DECL_PARM_INDEX rather than identity.
// DECL_PARM_LEVEL (cp-tree.h) is documented as "a parameter of the
// function will have level 1; a parameter of the first nested function
// declarator ... will have level 2" -- so a plain function-pointer
// VARIABLE's own nested parameters sit at level 1, while the SAME
// shape as a function PARAMETER's own nested parameters sit at level
// 2 (one level deeper, purely because the pointer itself is nested one
// level deeper). Two independent declarations with textually identical
// conditions therefore compared as "different" whenever one side
// happens to be more deeply nested than the other, producing a
// confusing false-positive warning whose text even names the same
// identifier on both sides when their names coincide (as 'p' does
// here). Fixed by a new, narrower comparing_contracts_ignore_parm_level
// flag, consulted only by contracts_equal_p's own comparisons, which
// drops the (here, irrelevant) absolute-level requirement while still
// requiring DECL_PARM_INDEX (position within the pointed-to callable's
// own parameter list) to match -- ordinary same-function redeclaration
// matching (mismatched_contracts_p) is untouched and stays strict.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -Wfunction-pointer-contract-mismatch" }

#include <contracts>

void f (int a, int b) {}

// The exact motivating shape: a contracted function-pointer VARIABLE
// passed to a function whose own, differently-nested PARAMETER has the
// identical contract text -- must stay silent.
void
g (void (*p) (int a, int b) pre<> (a <= b), int a, int b)
{
  p (a, b);
}

void
call_g ()
{
  void (*p) (int a, int b) pre<> (a <= b) = f;
  g (p, 1, 2);
  g (p, 2, 1);
}

// Same shape one level deeper on BOTH sides (the argument is itself a
// parameter, so both sides' own nested parameters sit at the same
// absolute level) -- already worked before the fix, confirms the fix
// doesn't disturb it.
void
h (void (*hp) (int a, int b) pre<> (a <= b))
{
  g (hp, 1, 2);
}

// A genuine mismatch at the same nested-parameter depth that triggered
// the false positive -- must still warn: the fix only relaxes the
// level requirement, never the actual condition-equality check.
void
i (void (*ip) (int a, int b) pre<> (a < b)) // different relation
{
  g (ip, 1, 2); // { dg-warning "contract" }
}

// Pointer-to-member-function analogue: the same cp_tree_equal path is
// shared by both features, so the same false positive (and fix) apply
// here too.
struct C {
  int field;
  int member (int x) { return field + x; }
};

void
gm (int (C::*p) (int x) pre<> (this != nullptr && x >= 0), C& obj, int x)
{
  (obj.*p) (x);
}

void
call_gm ()
{
  int (C::*p) (int x) pre<> (this != nullptr && x >= 0) = &C::member;
  C c;
  gm (p, c, 1);
}

void
im (int (C::*ip) (int x) pre<> (x > 0)) // different condition
{
  C c;
  gm (ip, c, 1); // { dg-warning "contract" }
}
