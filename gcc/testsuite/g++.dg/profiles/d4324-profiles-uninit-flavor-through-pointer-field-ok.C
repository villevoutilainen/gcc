// P4222 Initialization profile: a [[ref_to_uninit]]-flavored pointer
// FIELD, read by value (as opposed to a plain local/global pointer
// variable, already covered by d4324-profiles-uninit-flavor-through-
// pointer-var-cured-ok.C's own sibling tests), must be recognized as
// flavored too -- both at a single field-access level ('this->elem')
// and through a nested field chain ('mem.elem', the exact shape a
// user-reported false positive traced to: ip_arg_uninit_flavored_p_1
// (init-profile-gimple.cc) had cases for ADDR_EXPR, SSA_NAME, and a
// bare VAR_DECL/PARM_DECL, but none for a bare COMPONENT_REF, so a
// field's own [[ref_to_uninit]] attribute was silently discarded.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void other (int* q [[ref_to_uninit]]);

struct Direct {
  int* elem [[ref_to_uninit]];
  void f () { other (elem); }
};

struct Nested {
  Direct mem;
  void g () { other (mem.elem); }
};

void h (Direct& d, Nested& n)
{
  other (d.elem);
  other (n.mem.elem);
}
