// P3446R0/P4296R0 Invalidation profile: passing an [[owner]] value
// into an owner-*accepting* parameter (CE2) is a real consuming
// event, exactly like a direct delete -- reading through an earlier,
// untracked alias of that same value afterward is just as unsafe as
// reading through the original name, and is caught by leak point 4
// the same way.  Deliberately NOT the same as passing to a plain,
// non-owner parameter (see the companion -ok test): a function taking
// a non-owner pointer cannot invalidate that pointer at all.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f (int *p [[owner]]);

int use ()
{
  int *p [[owner]] = new int (7);
  int *q = p;
  f (p);
  return *q; // { dg-error "read here, after already being consumed" }
}
