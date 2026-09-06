// P4222 Initialization profile, worked demo #1 (also distributed as
// a standalone Compiler Explorer demo): three functions telling one
// story.
//
//  1) purely_safe():       happy path, everything initialized up
//                          front, no annotation needed at all.
//  2) needs_now_init():    happy path, but ONLY because of
//                          std::now_init() -- remove it and this
//                          function alone starts failing to compile
//                          with exactly the diagnostic (3) gets.
//  3) diagnosed():         the exact same shape as (2), without the
//                          assertion -- rejected by the profile.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

#include <utility>

// (1) Every local is initialized where it's declared -- nothing to
// annotate, nothing to prove.
int purely_safe (int a, int b)
{
  int sum = a + b;
  return sum;
}

// (2) 'x' is deliberately left uninitialized -- [[uninit]] says so
// explicitly, opting it out of the default "every object is
// initialized" guarantee. Handing its address to something the
// checker can't see into (a foreign write, placement construction
// through a different pointer, ...) means the checker still can't
// prove *p is readable -- std::now_init() is the manual "this is
// initialized now, trust me" assertion that lets it through.
int needs_now_init ()
{
  int x [[uninit]];
  int *p = std::now_init (&x);
  return *p;
}

// (3) Identical shape, no assertion -- diagnosed.  Also trips the
// GIMPLE-level assignment-flavor-consistency check, and taking x's
// address that way makes it unverifiable -- both now fire alongside
// the front-end declaration-time error, since the eager per-function
// checking mechanism no longer lets it suppress them.
int diagnosed ()
{
  int x [[uninit]]; // { dg-error "cannot verify" }
  int *p = &x; // { dg-error "which is marked" }
  // { dg-error "assigning a pointer marked" "" { target *-*-* } .-1 }
  return *p;
}
