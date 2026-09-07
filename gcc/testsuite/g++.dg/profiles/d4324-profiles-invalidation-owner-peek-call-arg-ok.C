// P3446R0/P4296R0 Invalidation profile: passing an [[owner]] value to
// an ordinary, non-owner-accepting parameter is a harmless "peek" --
// x alone remains responsible for its own consumption, unaffected by
// the call.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void peek (int *q);

void owner_arg_peek ()
{
  int *x [[owner]] = new int (666);
  peek (x);
  delete x;
}
