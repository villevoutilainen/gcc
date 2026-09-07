// P3446R0/P4296R0 Invalidation profile: leak point 4 (read after
// consumption) is caught even through a plain, UNTRACKED copy made
// before the consuming event -- y is never itself declared [[owner]]
// and never independently tracked, but its value still resolves back
// to x's own binding via ip_owner_resolve_origin's multi-hop walk, so
// reading through y after x is deleted is still flagged.  This used
// to be a documented, narrower residual gap (only closing for a
// known-alias hand-off into ANOTHER owner-marked variable); the
// generalized resolver closes it for an arbitrary unmarked alias too.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void read_after_untracked_alias_bad ([[owner]] int *x)
{
  int *y = x;
  delete x;
  int v = *y; // { dg-error "read here, after already being consumed" }
  (void) v;
}
