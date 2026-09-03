// P4222 Initialization profile, Phase 4e (S5.4): this pass has no
// per-member DAA for an arbitrary [[uninit]]-marked aggregate local
// the way it does for 'this' inside a constructor (Phase 4d) -- any
// direct member access on such a local is honestly declined rather
// than silently accepted as verified, even after a recognized
// [[must_init]] bulk-initialization call.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::init)]];

struct P {
  int x;
  int y;
};

void fill (P* q [[must_init]]);

void f ()
{
  [[uninit]] P p; // { dg-error "member-level access on this aggregate" }
  fill (&p);
  int x = p.x;
  (void) x;
}
