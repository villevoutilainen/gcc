// P3446R0/P4296R0 Invalidation profile: copying an [[owner]] value
// into ANOTHER, independently owner-marked local is itself a hand-off
// -- x's own binding is spent as soon as y takes over. A later use of
// x under its ORIGINAL name is therefore still a double-consumption,
// even though the two consuming statements name different variables
// -- without tracking this, x and y would read as two independent,
// both-satisfied obligations, silently missing a real double-free.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void caller ([[owner]] int *x)
{
  [[owner]] int *y = x;
  delete y;
  delete x; // { dg-error "consumed again here" }
}
