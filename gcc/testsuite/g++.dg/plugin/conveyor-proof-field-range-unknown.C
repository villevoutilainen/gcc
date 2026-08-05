// conveyor_proof_plugin.cc: t's count field was never established via
// a call to a function whose postcondition asserts a range for it --
// the plugin can't connect this to anything, so the best available
// answer is "cannot verify," not silent acceptance, and not a false
// claim of a proven violation either.  See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
// { dg-do run }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

struct thing {
  int count;
  void consume_count ()
    pre<conveyor_ctrl_v>(this->count >= 20 && this->count < 100)
  { }
};

int main ()
{
  thing t;
  t.count = 50;
  t.consume_count (); // { dg-warning "cannot verify" }
  return 0;
}
