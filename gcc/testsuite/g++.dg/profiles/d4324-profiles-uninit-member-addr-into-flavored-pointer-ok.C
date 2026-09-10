// P4222 Initialization profile: ip_scan_local_member_addr_uses (the
// escape-checker for '&var.field') previously recognized only "passed
// as a call argument to a must_init/ref_to_uninit parameter" as a safe
// use of the resulting address -- unlike ip_scan_stmt_for_var's own
// whole-object handling, it did not recognize "assigned directly into
// another declared-[[ref_to_uninit]] pointer variable" as safe at all,
// wrongly flagging 'p = &x.a;' itself as an unverifiable escape purely
// because p's only further use was a plain copy, not a call. Fixed to
// match the whole-object case's own identical exemption; flavor-
// consistency's own recursive chase (ip_arg_uninit_flavored_p_1) is what
// then verifies q itself, unrelated to this file.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct X { int a; int b; };

void f ()
{
  X x [[uninit]];
  int* p [[ref_to_uninit]] = &x.a;
  int* q [[ref_to_uninit]] = p;
  (void) q;
}
