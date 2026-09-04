// P3446R0/P4296R0 Invalidation profile, Phase 7a Negative Baseline:
// ordinary, unremarkable end-of-lifetime destruction -- block-scope
// exit and a delete-expression of an [[owning_ptr]] -- is NOT flagged
// by the explicit-destructor-call rule.  Both resolve, internally, to
// the exact same destructor *clones* a genuinely explicit "s.~S()"
// would (confirmed by direct -fdump-tree-original inspection during
// development), so this specifically exercises that the rule keys
// off the syntax the user wrote, not off which destructor clone ends
// up called.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct S { ~S() {} };

void f ()
{
  S s;
}

void g ([[owning_ptr]] S *p)
{
  delete p;
}
