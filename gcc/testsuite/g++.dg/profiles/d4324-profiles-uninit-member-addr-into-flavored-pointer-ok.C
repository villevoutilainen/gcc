// P4222 Initialization profile: the escape-checker for '&var.field'
// (ip_scan_stmt_for_local_member's own ADDR_EXPR-of-COMPONENT_REF case)
// now applies the same single-hop test as the whole-object case
// (ip_scan_stmt_for_var's ADDR_EXPR-of-VAR case): is 'p = &x.a;''s own
// destination, p, itself declared [[ref_to_uninit]]/[[must_init]]? If
// so this is simply the ordinary, fully-verified formation of a
// flavored pointer -- nothing about p's own subsequent use matters to
// this check at all (an earlier version instead walked every use of the
// resulting address, which both rejected an unused-but-flavored p and
// let an UNflavored intermediate pointer through based on what it was
// later passed to -- see git history for the full rationale). Here,
// flavor-consistency's own recursive chase (ip_arg_uninit_flavored_p_1)
// is what separately verifies 'q = p;' itself, unrelated to this file.
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
