// LOCAL-variable counterpart of the existing d4324-profiles-
// invalidation-owner-double-consume-reassign-ok.C (which covers a
// PARAMETER, protected by ip_check_owner_binding's own is_parameter-
// and-decl_reassigned guard). For a LOCAL binding there is no such
// guard: the reassignment itself must be recognized as a gen event by
// ip_owner_gen_lhs_decl, which resets the new leak-point-3/4 "maybe
// consumed" dataflow back to false, exactly like it already does for
// the older "may still be unconsumed" one. Must stay clean.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

[[owner]] int *g ();

void f ()
{
  int *p [[owner]] = new int{9};
  delete p;
  p = g ();
  delete p;
}
