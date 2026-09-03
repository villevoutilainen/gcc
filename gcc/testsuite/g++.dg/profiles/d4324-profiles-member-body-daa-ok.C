// P4222 Initialization profile, Phase 4d (S5.1-S5.3): the paper's own
// flagship example -- a [[uninit]] member not covered by the
// member-initializer-list, definitely assigned by every constructor
// exit path via straight-line body code -- is accepted.  So is
// assigning it through a recognized [[must_init]] call.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

int error_fn (int);
void initialize (int* q [[must_init]]);

struct StraightLine {
  int* p [[uninit]];
  int x;
  StraightLine (int v) : x{v}
  {
    if (v < 0)
      error_fn (v);
    p = new int(v);
  }
};

struct MustInitCall {
  int p [[uninit]];
  MustInitCall ()
  {
    initialize (&p);
  }
};

StraightLine make1 (int v) { return StraightLine(v); }
MustInitCall make2 () { return MustInitCall(); }
