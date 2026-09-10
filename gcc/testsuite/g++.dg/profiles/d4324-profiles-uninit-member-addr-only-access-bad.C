// P4222 Initialization profile: a field whose ONLY access anywhere in
// the function is &x.field (no other direct read/write of any field of
// x) must still be verified -- ip_scan_stmt_for_var's own MEMBER_ACCESS
// detection previously only recognized a direct read/write of x.field,
// never &x.field itself, so MEMBER_ACCESS never became true and the
// entire per-field escape-checking subsystem (ip_check_local_aggregate_
// member) was silently skipped for this exact shape. Confirmed as a
// real, silent gap before this fix: this file compiled completely
// clean. Companion to d4324-profiles-aggregate-member-address-escape-
// bad.C, which relies on an unrelated field write elsewhere to trip the
// same gate -- this file proves the gate no longer needs that decoy.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct X { int a; };

void take_ptr (int *);

void f ()
{
  X x [[uninit]];
  take_ptr (&x.a); // { dg-error "before it is provably initialized" }
}
