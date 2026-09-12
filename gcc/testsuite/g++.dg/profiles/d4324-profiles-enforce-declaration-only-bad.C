// P3589: profiles::enforce only ever has a well-formed meaning as its
// own bare empty-declaration ('[[profiles::enforce(profile)]];').
// Without the trailing ';', the attribute-specifier-seq attaches to
// the FOLLOWING declaration instead -- here an ordinary variable,
// reaching decl_attributes the normal way (handle_profiles_
// declaration_only_attribute, tree.cc) -- and must be diagnosed
// clearly, rather than the generic, unrelated-looking "attribute
// ignored" warning this used to silently produce while std::init was
// never actually enforced for the rest of the translation unit at
// all.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]]

int global_var; // { dg-error "only appertains to its own" }
