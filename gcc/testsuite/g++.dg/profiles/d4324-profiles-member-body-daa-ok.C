// P4222 Initialization profile, Phase 4d (S5.1-S5.3): for a member
// marked literally [[uninit]] (not just [[ref_to_uninit]]/
// [[must_init]]), the constructor is never required to assign it at
// all -- the entire point of [[uninit]] is "no promise is made here,
// not even by the constructor," verified later at whatever point
// something actually reads it, exactly like a plain [[uninit]] local
// that's simply never read. Writing it anyway (straight-line body
// code, conditionally, or via a recognized [[must_init]] call) is
// just as accepted, since it's always legal to actually initialize
// something you were merely permitted to leave alone.
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

struct AssignedOnOneBranchOnly {
  int* p [[uninit]];
  int x;
  AssignedOnOneBranchOnly (int v) : x{v}
  {
    if (v < 0)
      p = new int(0);
    // else: p left uninitialized on this path -- fine, nothing reads
    // it here, and [[uninit]] makes no promise about it either way.
  }
};

struct NeverAssignedAtAll {
  int* p [[uninit]];
  int x;
  NeverAssignedAtAll (int v) : x{v} {}
};

StraightLine make1 (int v) { return StraightLine(v); }
MustInitCall make2 () { return MustInitCall(); }
AssignedOnOneBranchOnly make3 (int v) { return AssignedOnOneBranchOnly(v); }
NeverAssignedAtAll make4 (int v) { return NeverAssignedAtAll(v); }
