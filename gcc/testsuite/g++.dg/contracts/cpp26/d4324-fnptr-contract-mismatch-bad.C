// D4324: -Wfunction-pointer-contract-mismatch fires on the two cases
// where a function-pointer copy can silently change which contract
// governs calls made through the destination, or is at least likely a
// mistake: (1) the source has its own contract but the destination has
// none at all (a real, silent loss of enforcement -- nothing will
// check the source's contract once called only through the
// uncontracted destination); (3) both sides have contracts, but they
// differ (the destination's contract still governs at run time, but
// this is likely a mistake worth flagging). See d4324-fnptr-contract-
// mismatch-ok.C for the cases that must stay silent.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -Wfunction-pointer-contract-mismatch" }

#include <contracts>

int f (int x) pre<> (x > 0) { return x; }
int h (int x) pre<> (x > 1) { return x; }

// Case 1: source contracted, destination plain.
int (*plain_fp) (int x);

void
assign_case_1 ()
{
  plain_fp = f;			// { dg-warning "contract" }
  plain_fp = &f;		// { dg-warning "contract" }
}

void
init_case_1 ()
{
  int (*init_fp) (int x) = f;	// { dg-warning "contract" }
}

void call_arg_case_1 (int (*p) (int x));

void
call_case_1 ()
{
  call_arg_case_1 (f);		// { dg-warning "contract" }
}

// Case 3: both contracted, different conditions.
int (*fp_g) (int x) pre<> (x > 0);

void
assign_case_3 ()
{
  fp_g = h;			// { dg-warning "contract" }
}

void call_arg_case_3 (int (*p) (int x) pre<> (x > 0));

void
call_case_3 ()
{
  call_arg_case_3 (h);		// { dg-warning "contract" }
}

// NSDMI: non-static data members can't carry an object-level contract
// of their own in this fork (only VAR_DECL/PARM_DECL can), so this is
// another instance of case 1: the field is (necessarily) plain, and
// h's own contract is silently unenforced once called only through it.
struct T
{
  int (*mfp) (int x) = h;	// { dg-warning "contract" }
};
