// conveyor_proof_plugin.cc: a two-conjunct bare-scalar precondition
// ('x >= 0 && x <= 100') combined into one overall verdict before
// diagnosing, mirroring contracts.cc's own oa_handle_precondition_
// simple_range_obligation (which groups every such conjunct about the
// same parameter into a single [lo,hi] interval and diagnoses that
// once) -- this plugin previously checked and diagnosed each conjunct
// independently, so this exact shape used to produce two separate
// "cannot verify" warnings where the built-in engine emits (at most)
// one. See .claude/plans/lazy-stirring-pearl.md.
// { dg-do run }
// { dg-additional-sources "conveyor-proof-defs.cc" }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include "conveyor-proof-defs.h"

void use_bounded (int x) pre<conveyor_ctrl_v>(x >= 0 && x <= 100) {}

void caller (int untrusted)
{
  use_bounded (untrusted); // { dg-warning "cannot verify" }
}

int main () { caller (1); return 0; }
