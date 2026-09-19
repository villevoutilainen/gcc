// Companion to d4324-profiles-must-init-body-bad.C: a function
// actually fulfilling its own [[must_init]] promise -- unconditional
// straight-line write, both-branch write, and delegation to another
// [[must_init]]-flavored function -- must produce no diagnostic.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

void straight_line (int* p [[must_init]])
{
  *p = 0;
}

void both_branches (int* p [[must_init]], bool c)
{
  if (c)
    *p = 1;
  else
    *p = 2;
}

void helper (int* q [[must_init]]);

void delegate (int* p [[must_init]])
{
  helper (p);
}
