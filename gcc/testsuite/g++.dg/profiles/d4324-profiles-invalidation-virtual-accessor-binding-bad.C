// P3446R0/P4296R0 Invalidation profile: a virtually-dispatched
// accessor (a call whose own callee is only known indirectly, through
// OBJ_TYPE_REF) establishes a binding to its receiver exactly like a
// directly-resolved one already does -- ip_binding_established_by
// falls back to ip_virtual_call_declared_target when gimple_call_
// fndecl can't resolve the callee directly, so a polymorphic
// 'begin()'-shaped member is tracked too, not silently ignored.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct Poly
{
  virtual int *begin ();
  virtual void push_back (int);
  virtual ~Poly () {}
};

void f (Poly &v1, Poly &v2, bool c)
{
  int *p;
  if (c)
    {
      v1.push_back (99);   // mutates v1, BEFORE p is bound to v1
      p = v1.begin ();      // binds p -> v1, via virtual dispatch
    }
  else
    {
      p = v2.begin ();      // binds p -> v2, via virtual dispatch
      v2.push_back (99);    // mutates v2, AFTER p is bound to v2
    }
  int x = *p; // { dg-error "potentially invalidated by an earlier mutation" }
  (void) x;
}
