// Generalization of d4324-profiles-invalidation-owner-mutually-
// exclusive-conds-ok.C beyond a bare boolean: 'x >= 0' and 'x < 0'
// are the exact logical inverse of each other (invert_tree_
// comparison), same operand, so exactly one of these two deletes
// ever executes. A delete-expression's own implicit null-guard means
// each "arm" here is itself a small diamond, not a single block --
// this is what confirmed the dominance-based predecessor-exclusion
// design (rather than a simpler chain-walk) was actually necessary,
// not just a boolean-specific convenience. Must stay clean.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

void f (int *p [[owner]], int x)
{
  if (x >= 0) delete p;
  if (x < 0) delete p;
}
