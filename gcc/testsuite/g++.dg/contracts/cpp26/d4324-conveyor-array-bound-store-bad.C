// D4324/P2680 item 8, Increment W2 fix: oa_walk_stmt's own MODIFY_EXPR/
// INIT_EXPR case previously only ever scanned its own RHS
// (oa_scan_item8_in_expr (&TREE_OPERAND (t, 1), env)) for item 8's
// mandatory div/mod, array-bound, and overflow restrictions -- its own
// LHS was never scanned at all, so a store *through* an unprovable
// pointer, or field access through one, on the assignment TARGET itself
// was silently unchecked (found while porting the GIMPLE-side analogue,
// cg_check_dereference_ub, which checks a GIMPLE_ASSIGN's own LHS as
// well as its RHS1). Confirmed by direct testing that both shapes
// compiled cleanly before this fix. Now scans both sides.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

void deref_store_bad (int *p) conveyor
{
  *p = 5; // { dg-error "pointer dereference of .p. not provably valid in a conveyor function" }
}

/* A field access through a pointer ('p->v') compiles to a synthesized
   INDIRECT_REF with no location of its own: the error takes the
   statement's.  */
struct T { int v; };

void field_store_bad (T *p) conveyor
{
  p->v = 5; // { dg-error "pointer dereference of .p. not provably valid in a conveyor function" }
}

int main () { return 0; }
