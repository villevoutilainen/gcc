// P3446R0/P4296R0 Invalidation profile: a mutating call reached
// through virtual dispatch on one, provably-unrelated type must not
// affect a binding tracked against a completely different type --
// Rule #0's own type-relatedness reasoning applies identically whether
// the mutating call was resolved directly or via
// ip_virtual_call_declared_target.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct Poly
{
  virtual void mutate ();
  virtual ~Poly () {}
};

struct Vector { int *data (); void push_back (int); };

void f (Poly &obj, bool c)
{
  Vector v1{}, v2{};
  int *p;
  if (c)
    {
      p = v1.data ();
      obj.mutate ();   // virtual dispatch, but obj is provably unrelated to Vector
    }
  else
    p = v2.data ();
  int x = *p;
  (void) x;
}
