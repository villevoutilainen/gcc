// conveyor_proof_plugin.cc: untrusted was never established via a call
// to a function whose postcondition asserts check_it for its own
// result -- the plugin can't connect this to anything, so the best
// available answer is "cannot verify," not silent acceptance, and not
// a false claim of a proven violation either.  See
// .claude/plans/stateless-jumping-shore.md.
// { dg-do run }
// { dg-additional-sources "conveyor-proof-defs.cc" }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void caller2 (int untrusted)
{
  consume (untrusted); // { dg-warning "cannot verify" }
}

int main () { caller2 (1); return 0; }
