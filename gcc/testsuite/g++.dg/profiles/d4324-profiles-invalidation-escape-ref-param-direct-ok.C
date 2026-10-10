// P3446R0 Invalidation profile: contrast case for the sibling
// escape-ref-param-direct-bad.C test -- a plain, non-const lvalue
// reference parameter cannot bind an rvalue/temporary at all, so its
// referent genuinely outlives this call; returning its address must
// stay clean. Confirms the widened dispatch in ip_escapes_locally_p's
// own SSA_NAME handling (now routing every reference-shaped
// parameter, not just DECL_BY_REFERENCE ones, through ip_local_var_p)
// still correctly separates this case from the sibling test's two
// genuinely-dangling ones -- ip_local_var_p's own existing logic
// already reports a plain T& as NOT local; this test exercises that
// it's actually reached and still answered correctly via the new,
// broadened call site.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

int* f_plain_lref (int& x)
{
  return &x;
}
