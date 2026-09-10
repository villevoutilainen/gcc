// P4222 Initialization profile: the union counterpart of
// d4324-profiles-uninit-member-addr-only-access-bad.C -- a union field
// whose only access anywhere is &u.field must still trip the
// "member-level access on this aggregate is not yet analyzed" honest
// decline, not silently compile clean because MEMBER_ACCESS was never
// set for an address-of-only field access.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

union U { int a; float b; };

void take_ptr (int *);

void f ()
{
  U u [[uninit]]; // { dg-error "member-level access on this aggregate is not yet analyzed" }
  take_ptr (&u.a);
}
