// D4324: -Wfunction-pointer-contract-mismatch stays silent on the
// cases that are either not a genuine loss of enforcement, or that
// can't be determined at all: (2) destination has its own contract,
// source doesn't -- the ordinary, intended object-decl-callable idiom
// (see d4324-object-decl-callable-enforcement.C), since the
// destination's own contract keeps governing regardless of what's
// assigned to it; both sides have the exact same contract, or neither
// has one; the source is a computed/complex expression we can't
// resolve to a decl at all; and an explicit cast, which always
// silences the warning regardless of the above. See d4324-fnptr-
// contract-mismatch-bad.C for the cases that must warn.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -Wfunction-pointer-contract-mismatch" }

#include <contracts>

int f (int x) pre<> (x > 0) { return x; }
int plain_fn (int x) { return x; }
int other_plain_fn (int x) { return x; }
void plain_void (int x) { }

// Case 2: destination contracted, source plain -- the intended idiom.
int (*fp_ctrl) (int x) pre<> (x > 0);

void
assign_case_2 ()
{
  fp_ctrl = plain_fn;
}

int (*init_fp) (int x) pre<> (x > 0) = plain_fn;

void call_arg_case_2 (int (*p) (int x) pre<> (x > 0));

void
call_case_2 ()
{
  call_arg_case_2 (plain_fn);
}

// Both sides have exactly the same contract: silent.
int (*fp_same) (int x) pre<> (x > 0) = f;

// Neither side has a contract: silent.
void (*fp_none) (int x) = plain_void;

// Can't resolve the source to a decl at all: silent, not "assume none".
int (*fp_via_ternary) (int x) = true ? plain_fn : other_plain_fn;

// Explicit cast silences the warning even though f has a contract of
// its own and the destination has none (case 1, which otherwise warns
// -- see d4324-fnptr-contract-mismatch-bad.C).
int (*fp_static_cast) (int x) = static_cast<int (*) (int x)> (f);
int (*fp_c_cast) (int x) = (int (*) (int x)) f;
int (*fp_reinterpret_cast) (int x)
  = reinterpret_cast<int (*) (int x)> (&f);

// NSDMI: confirms the explicit-cast escape hatch also applies at the
// digest_nsdmi_init call site, not just plain variable initialization
// (non-static data members can't carry an object-level contract of
// their own in this fork, so mfp is necessarily plain here; without
// the cast this would otherwise be case 1 -- see d4324-fnptr-contract-
// mismatch-bad.C's own NSDMI case).
struct T
{
  int (*mfp) (int x) = static_cast<int (*) (int x)> (f);
};
