// P3446R0/P4296R0 Invalidation profile: a const call reached only
// through virtual dispatch must NOT be classified as mutating -- the
// declared constness of the vtable slot's own function is invariant
// across every override, per [class.virtual], so this is exactly as
// safe to trust as it is for a directly-resolved const call.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct Poly
{
  virtual int *data ();
  virtual void observe () const;
  virtual ~Poly () {}
};

void f (Poly &obj)
{
  int *p = obj.data ();   // binds p to obj
  obj.observe ();         // const virtual call -- must not invalidate p
  int x = *p;
  (void) x;
}
