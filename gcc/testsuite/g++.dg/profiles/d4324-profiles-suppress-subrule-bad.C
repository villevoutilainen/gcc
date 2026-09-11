// P3589: profiles::suppress's optional 'rule: "..."' argument names a
// single sub-rule of the profile to suppress instead of the whole
// thing. Neither std::init nor std::invalidation register any
// sub-rules today, so any 'rule:' argument must be cleanly rejected --
// not silently accepted, and above all not crash the compiler the way
// an earlier version of cp_parser_profiles_attribute_args's malformed-
// argument recovery did (its final "expected ')'" error path never
// skipped to the closing parenthesis, leaving a stray ", rule: ...)"
// in the token stream that eventually fed an error_mark_node into
// profiles_process_suppress_attributes's TREE_VALUE (TREE_VALUE (attr))
// unchecked).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

[[profiles::suppress(std::init, rule: "ref_to_uninit")]]
void f (); // { dg-error "has no sub-rule" }

[[profiles::suppress(std::init, rule: "x", justification: "because")]]
void g (); // { dg-error "has no sub-rule" }
