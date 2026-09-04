// P4222 Initialization profile, Phase 4f: per-FIELD DAA is genuinely
// per field -- writing ONE field of an [[uninit]] local aggregate
// does not vouch for a DIFFERENT field that was never written at all.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct P {
  int x [[uninit]];
  int y [[uninit]];
};

int f ()
{
  [[uninit]] P p;
  p.x = 1;
  return p.y; // { dg-error "read before it is definitely assigned" }
}
