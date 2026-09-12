// P4222 Initialization profile: companion to d4324-profiles-uninit-
// flavor-through-pointer-field-ok.C -- confirms the COMPONENT_REF fix
// there checks the field's OWN [[ref_to_uninit]] attribute rather than
// unconditionally treating any field read as flavored. An ordinary,
// unflavored pointer field must still be rejected when passed to a
// [[ref_to_uninit]] parameter, at both a single field-access level and
// through a nested field chain.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void other (int* q [[ref_to_uninit]]);

struct Direct {
  int* elem;
  void f () { other (elem); } // { dg-error "must refer to" }
};

struct Nested {
  Direct mem;
  void g () { other (mem.elem); } // { dg-error "must refer to" }
};
