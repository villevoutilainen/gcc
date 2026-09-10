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
// not itself what this test is about; a plain, unused p would trip
// that check on its own narrower "every use of p must itself be a
// flavored/must_init call argument" terms, a different, not-yet-fixed
// limitation (this checker doesn't yet recognize "assigned into
// another declared-flavored pointer variable" as safe for a member the
// way it already does for a whole object) -- avoided here rather than
// conflated with what this test is actually verifying.
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
