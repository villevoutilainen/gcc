// P3446R0/P4296R0 Invalidation profile: [[not_invalidating]] on a
// virtual function's declared, statically-resolved slot is
// deliberately NOT honored when the call reaching it is only known
// through virtual dispatch -- unlike constness, nothing in the
// language enforces that every override of a [[not_invalidating]]
// virtual function is itself non-invalidating, so trusting a marking
// found only via the receiver's static type would be unsound.  A call
// resolved this way is conservatively still treated as mutating.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct Poly
{
  virtual int *data ();
  [[not_invalidating]] virtual void maybe_mutate ();
  virtual ~Poly () {}
};

void f (Poly &obj)
{
  int *p = obj.data ();
  obj.maybe_mutate ();   // virtual dispatch -- [[not_invalidating]] not trusted
  int x = *p; // { dg-error "potentially invalidated by an earlier mutation" }
  (void) x;
}
