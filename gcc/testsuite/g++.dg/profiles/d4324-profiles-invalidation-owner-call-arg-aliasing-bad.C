// P3446R0/P4296R0 Invalidation profile: passing the SAME [[owner]]
// pointer to two DIFFERENT owner-accepting parameters of one call is
// a real hazard the definite-consumption layer alone can't see -- two
// [[owner]] parameters declare two INDEPENDENT ownership obligations,
// so a callee that (reasonably) deletes each separately double-frees
// when handed the same pointer twice.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f (int *p [[owner]], int *q [[owner]]);

void g ([[owner]] int *ptr)
{
  f (ptr, ptr); // { dg-error "passed to two different owner-accepting parameters" }
}
