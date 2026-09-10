// P4222 Initialization profile: flavor-consistency's COMPONENT_REF
// rule must gate on whether the FIELD is subject to uninit-tracking at
// all (the field itself individually marked [[uninit]], OR the whole
// containing variable marked [[uninit]]), not on whether the field
// carries the attribute directly -- a field of a whole-[[uninit]]
// aggregate never carries [[uninit]] itself (only the local variable
// does), so gating on the field's own attribute alone wrongly rejected
// this exact, legitimate pattern as a flavor mismatch before this fix
// (found while investigating why this case double-diagnosed
// differently from the scalar one). consume()'s own [[ref_to_uninit]]
// parameter is what p is then used for -- a recognized-safe shape for
// the SEPARATE address-escape check (ip_scan_local_member_addr_uses),
// not itself what this test is about; an entirely unused p would still
// trip that check regardless (it requires at least one recognized-safe
// use, not zero), so a real use is needed here either way -- avoided
// conflating that with what this test is actually verifying. See
// d4324-profiles-uninit-member-addr-into-flavored-pointer-ok.C for the
// "assigned into another declared-flavored pointer variable" shape.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct X { int a; int b; };

void consume (int* p [[ref_to_uninit]]);

void f ()
{
  X x [[uninit]];
  int* p [[ref_to_uninit]] = &x.a;
  consume (p);
}
