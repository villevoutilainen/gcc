// D4324: -Wfunction-pointer-contract-mismatch, extended to pointer-to-
// member-function sources. The destination-type gate (maybe_warn_
// fnptr_contract_mismatch) already listed TYPE_PTRMEMFUNC_P, but the
// source resolver (fnptr_contract_owner) never recognized a PTRMEM_CST
// (what '&C::member' produces for a non-static member), so this half
// of the warning was unreachable dead code -- fnptr_contract_owner now
// returns PTRMEM_CST_MEMBER directly for a PTRMEM_CST source. Mirrors
// d4324-fnptr-contract-mismatch-bad.C's own case 1 (source contracted,
// destination plain) and case 3 (both contracted, differing), plus the
// silent case (both contracted, same condition) that must stay quiet.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -Wfunction-pointer-contract-mismatch" }

#include <contracts>

struct C {
  int field;
  int member (int x) pre<> (x > 0) { return field + x; }
  int other (int x) pre<> (x > 1) { return field + x; }
  int plain (int x) { return field + x; }
};

// Case 1: source (&C::member) contracted, destination plain.
int (C::*plain_mfp) (int x);

void
assign_case_1 ()
{
  plain_mfp = &C::member;	// { dg-warning "contract" }
}

void
init_case_1 ()
{
  int (C::*init_mfp) (int x) = &C::member;	// { dg-warning "contract" }
}

void call_arg_case_1 (int (C::*p) (int x));

void
call_case_1 ()
{
  call_arg_case_1 (&C::member);	// { dg-warning "contract" }
}

// Case 3: both contracted, different conditions.
int (C::*mfp_g) (int x) pre<> (x > 0);

void
assign_case_3 ()
{
  mfp_g = &C::other;		// { dg-warning "contract" }
}

// Silent: destination plain, source plain -- nothing to warn about.
void
silent_both_plain ()
{
  plain_mfp = &C::plain;
}

// Silent: both contracted, identical condition.
int (C::*mfp_h) (int x) pre<> (x > 0);

void
silent_same_contract ()
{
  mfp_h = &C::member;
}
