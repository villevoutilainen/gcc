// P3446R0/P4296R0 Invalidation profile, flavor-consistency layer: a
// plain assignment of an owner-flavored pointer into an unmarked one
// is a mismatch.  q also becomes a tracked binding in its own right
// (it receives an owner-flavored value, regardless of its own,
// unmarked, declaration -- see ip_owner_gen_lhs_decl's own comment,
// invalidation-profile-gimple.cc), and is separately, correctly
// flagged as never consumed.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f ([[owner]] int *p)
{
  int *q = p; // { dg-error "assigning a pointer marked" }
  // { dg-error "never deleted or passed on" "" { target *-*-* } .-1 }
  (void) q;
  delete p;
}
