// P3446R0/P4296R0 Invalidation profile: the reverse-direction
// call-argument check accepts a multi-hop-resolved fresh origin, not
// just a literal [[owner]]-marked argument -- the call-argument
// analog of the assignment-direction multihop-chain-ok test.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void sink ([[owner]] int *q);

void reverse_accept_multihop_arg_ok ()
{
  int *a = new int (1);
  int *b = a;
  sink (b);
}
