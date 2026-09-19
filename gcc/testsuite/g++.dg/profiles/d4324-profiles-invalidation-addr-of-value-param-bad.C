// Minimal, non-library-dependent companion to d4324-profiles-invalidation-
// string-view-of-value-param-bad.C: the other GIMPLE shape reaching the
// same ip_local_var_p gap -- a real, explicit ADDR_EXPR of a plain
// (non-reference) by-value parameter, which ip_escapes_locally_p's own
// ADDR_EXPR branch used to reject outright via its 'VAR_P (base)' gate
// (a PARM_DECL is a different tree code from VAR_DECL).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct View
{
  const int *p;
  View (const int *q) : p (q) {}
};

View f (int x)
{
  return View (&x); // { dg-error "may hold a pointer to a local" }
}
