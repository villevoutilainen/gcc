// P3446R0 Invalidation profile: a lambda capturing a local by reference,
// itself returned from the enclosing function, is a genuine container-
// escape and must be diagnosed -- confirmed a real, reliably reproducible
// ICE (segfault in gimplify_compound_lval/is_gimple_min_invariant): the
// eager per-function checker (profiles_eager_check_function, profiles.cc)
// reached the lambda's own call operator (via finish_lambda_function's own
// expand_or_defer_fn call, lambda.cc) BEFORE the enclosing cp_parser_
// lambda_expression (parser.cc) had called finish_struct on the closure
// type -- catching the const-qualified variant of the closure type (used
// for operator() const's own 'this' parameter) with stale, pre-completion
// TYPE_FIELDS. Fixed by deferring eager-checking of any function whose own
// DECL_CONTEXT is still incomplete, retried once finish_struct actually
// completes it (profiles_eager_check_type_complete).
// { dg-do compile { target c++14 } }

[[profiles::enforce(std::invalidation)]];

auto f ()
{
  int x = 4;
  return [&] (int y) { return x = y; }; // { dg-error "may hold a pointer to a local" }
}

void g ()
{
  auto lam = f ();
  int z = lam (7);
  (void) z;
}
