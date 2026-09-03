// P4222 Initialization profile, Phase 3: real CFG-dominance-based
// DAA for a [[uninit]] local's [[must_init]] initializing calls -- a
// call on only one branch does not dominate a read reachable via the
// other branch.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int use (int);
void initialize (int* q [[must_init]]);

void one_branch_only (bool c)
{
  [[uninit]] int x;
  if (c)
    initialize (&x);
  use (x); // { dg-error "read before it is definitely assigned" }
}

void no_init_at_all ()
{
  [[uninit]] int x;
  use (x); // { dg-error "read before it is definitely assigned" }
}
