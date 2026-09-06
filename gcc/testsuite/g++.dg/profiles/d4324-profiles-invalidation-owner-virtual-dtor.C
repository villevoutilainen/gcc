// P3446R0/P4296R0 Invalidation profile: deleting an [[owner]] pointer
// to a class with a VIRTUAL destructor is recognized as consuming it,
// the same as a non-virtual one -- even though the actual
// deallocation happens inside the deleting destructor's own
// synthesized clone, reached here via an indirect (vtable) call with
// no directly resolvable callee.  Recognized via CALL_FROM_NEW_OR_
// DELETE_P, which build_delete (init.cc) now marks on the deleting-
// destructor call it builds, and which gimple.cc's own gimplify_call_
// expr now propagates onto the resulting GIMPLE_CALL regardless of
// whether the call ends up direct or virtual/indirect.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct Base { virtual ~Base () {} };
struct Derived : Base {};

void deleted_ok ([[owner]] Base *p) { delete p; }

void one_branch_bad ([[owner]] Base *p, bool c) // { dg-error "never deleted or passed on" }
{
  if (c)
    delete p;
}

void both_branches_ok ([[owner]] Base *p, bool c)
{
  if (c)
    delete p;
  else
    delete p;
}
