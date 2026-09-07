// P3446R0/P4296R0 Invalidation profile: the multi-hop resolver
// (ip_owner_resolve_origin) catches a double-consumption reached
// through a chain of TWO known-alias hand-offs, not just one -- x's
// own binding is spent as soon as y takes over, and again as soon as
// z takes over from y, so consuming it a third time under x's own
// original name is still a double-consumption.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void multihop_double_consume_bad ([[owner]] int *x)
{
  [[owner]] int *y = x;
  [[owner]] int *z = y;
  delete z;
  delete x; // { dg-error "consumed again here" }
}
