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

// (3) Identical shape, no assertion -- diagnosed.
int diagnosed ()
{
  int x [[uninit]];
  int *p = &x; // { dg-error "not marked" }
  return *p;
}
