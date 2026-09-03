// P4222 Initialization profile, Phase 4e (S5.4): a fully-trivial
// aggregate declared without an initializer must be marked [[uninit]],
// the same rule as a scalar (Phase 2) or an array (Phase 4a).
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct P {
  int x;
  int y;
};

void f ()
{
  P p; // { dg-error "not initialized and not marked" }
  (void) p;
}
