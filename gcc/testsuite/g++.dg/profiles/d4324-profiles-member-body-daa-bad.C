// P4222 Initialization profile, Phase 4d (S5.1-S5.3): real
// CFG-dominance-based DAA for a [[uninit]] member -- a constructor
// that leaves it unassigned on some path (or reads it before writing
// it) is rejected, the same way an address-taken [[uninit]] local is
// (Phase 3), just checked through 'this' instead of a plain variable.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct OneBranchOnly {
  int* p [[uninit]];
  int x;
  OneBranchOnly (int v) : x{v} // { dg-error "not definitely assigned before" }
  {
    if (v < 0)
      p = new int(0);
    // else: p left uninitialized on this path
  }
};

struct NeverInit {
  int* p [[uninit]];
  int x;
  NeverInit (int v) : x{v} {} // { dg-error "not definitely assigned before" }
};

struct ReadBeforeWrite {
  int* p [[uninit]];
  int x;
  ReadBeforeWrite (int v) : x{v}
  {
    int* q = p; // { dg-error "read before it is definitely assigned" }
    p = new int(v);
    (void) q;
  }
};

OneBranchOnly make1 (int v) { return OneBranchOnly(v); }
NeverInit make2 (int v) { return NeverInit(v); }
ReadBeforeWrite make3 (int v) { return ReadBeforeWrite(v); }
