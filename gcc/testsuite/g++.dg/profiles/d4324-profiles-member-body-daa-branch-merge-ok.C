// P4222 Initialization profile: real forward "must reach" dataflow
// (ip_compute_reach_info, init-profile-gimple.cc, added 2026-09-04)
// for the "every constructor RETURN dominated by an init" requirement
// too (ip_check_constructor_member's own exit_ok check) -- a
// [[ref_to_uninit]] member assigned on every arm of an if/else, with
// a single shared return point after the if/else (not one return per
// branch), is accepted: neither branch's own write individually
// dominates that shared exit-predecessor block, but every path into
// it does pass through one of them.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct S {
  int m1;
  int* m2 [[ref_to_uninit]];
  S (int v)
    : m1{v}
  {
    if (v < 0)
      m2 = new int (0);
    else
      m2 = new int (1);
  }
};

S make (int v) { return S (v); }
