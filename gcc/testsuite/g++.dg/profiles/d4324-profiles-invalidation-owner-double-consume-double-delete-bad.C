// P3446R0/P4296R0 Invalidation profile: the double-consumption check
// also catches the plainest shape of all -- deleting the very same
// [[owner]] parameter twice in a row, no intervening calls at all.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void caller ([[owner]] int *x)
{
  delete x;
  delete x; // { dg-error "consumed again here" }
}
