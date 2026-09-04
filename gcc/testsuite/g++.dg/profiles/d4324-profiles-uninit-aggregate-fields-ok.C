// P4222 Initialization profile, Phase 4f: an [[uninit]]-marked local
// aggregate can genuinely be filled in field-by-field (in whichever
// order, across however many statements) before being read -- real
// per-FIELD CFG-dominance-based DAA (ip_check_local_aggregate_member,
// init-profile-gimple.cc), not the unconditional "member-level access
// is not yet analyzed" decline this exact shape used to get.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct P {
  int x [[uninit]];
  int y [[uninit]];
};

void f ()
{
  [[uninit]] P p;
  p.x = 1;
  p.y = 2;
  int z = p.x + p.y;
  (void) z;
}
