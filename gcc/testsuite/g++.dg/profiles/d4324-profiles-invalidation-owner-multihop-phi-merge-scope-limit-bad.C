// P3446R0/P4296R0 Invalidation profile: a documented, permitted
// precision limit of ip_owner_resolve_origin's own multi-hop walk --
// it resolves precisely through an unambiguous, straight-line chain
// of plain copies, but deliberately does not descend through a
// genuine PHI merge of two DIFFERENT fresh origins (the same
// "documented, permitted incompleteness" precedent this file's own
// mechanical spec already accepts for pointer-arithmetic chain
// resolution, S5.4).  q's own binding IS still correctly tracked
// (established via the PHI-merge branch of ip_arg_owner_flavored_p_1,
// unaffected by this limit), and is properly consumed via 'delete p;'
// in a from-q's-own-perspective sense -- but the reverse-direction
// genuineness check at the 'p = r;' assignment can't see that far
// back through the merge, and conservatively rejects it.  A real,
// accepted false positive in this one narrow shape, not a soundness
// gap (rejecting more than strictly necessary is always safe).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void multihop_phi_merge_scope_limit_bad (bool c)
{
  int *q = c ? new int (1) : new int (2);
  int *r = q;
  [[owner]] int *p = r; // { dg-error "assigning a pointer not marked" }
  delete p;
}
