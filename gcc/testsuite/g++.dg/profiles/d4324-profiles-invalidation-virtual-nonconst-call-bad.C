// P3446R0/P4296R0 Invalidation profile: a non-const call reached only
// through virtual (indirect, OBJ_TYPE_REF) dispatch is classified as
// mutating its receiver, exactly like a directly-resolved non-const
// member call already is -- ip_virtual_call_declared_target resolves
// the FUNCTION_DECL declared at the dispatched-through vtable slot, on
// the receiver's own STATIC type, and [class.virtual]'s own override
// rules guarantee that declaration's cv-qualification is identical to
// whatever override actually runs at that slot, regardless of dynamic
// type.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct Poly
{
  virtual int *data ();
  virtual void mutate ();
  virtual ~Poly () {}
};

void f (Poly &obj)
{
  int *p = obj.data ();   // binds p to obj (also via virtual dispatch)
  obj.mutate ();          // non-const virtual call -- mutates obj
  int x = *p; // { dg-error "potentially invalidated by an earlier mutation" }
  (void) x;
}
