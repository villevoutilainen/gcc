// P3446R0 Invalidation profile: a genuine, previously-untested gap in
// ip_local_var_p's own const-ref/rvalue-ref extension. That extension
// was originally verified only via an ADDR_EXPR of a LOCAL reference
// variable needing a SECOND materialization to bind yet another
// function's own reference parameter (e.g. 'container.push_back
// (&ref)'), never via a reference PARAMETER's own value read
// DIRECTLY, with nothing else involved. Confirmed via gdb: since x is
// already reference-typed, '&x' lowers to simply reading x's own SSA
// value (a reference already holds its referent's address natively),
// producing no ADDR_EXPR at all -- so the existing ADDR_EXPR-based
// check could never have caught this shape regardless of ip_local_
// var_p's own logic. Both cases below must be flagged: a const
// lvalue reference or an rvalue reference may be bound to a caller's
// temporary, whose lifetime ends by the time this function returns.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

const int* f_const_lref (const int& x)
{
  return &x; // { dg-error "may hold a pointer to a local" }
}

int* f_rref (int&& x)
{
  return &x; // { dg-error "may hold a pointer to a local" }
}
