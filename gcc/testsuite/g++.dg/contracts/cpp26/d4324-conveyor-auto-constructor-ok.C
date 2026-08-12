// D4324: a 'conveyor(auto)' constructor must deduce correctly despite
// GCC's own constructor cloning (a single user-written constructor is
// split into multiple clones -- complete-object, base-object, etc. --
// an ABI implementation detail; see build_cdtor_clones in class.cc).
// instantiate_decl redirects a clone to the function it was cloned
// from before substituting anything ("don't instantiate cloned
// functions, instead instantiate the functions they cloned"), so
// maybe_instantiate_conveyor's own deduction pass only ever resolves
// DECL_DECLARED_CONVEYOR_P/DECL_CONVEYOR_AUTO_RESOLVED_P on that
// un-cloned function -- a clone's own copy of those bits, taken once
// when the clone was created, is never updated by that alone, so a
// callee check that happens to operate on a specific clone's own decl
// (as a constructor call's does) must see the resolved answer
// propagated back onto it explicitly. Found as a real bug: this exact
// shape, with a trivially-safe body (nothing but a direct member
// initialization), failed to deduce conveyor at all until
// maybe_instantiate_conveyor was fixed to sync every clone after
// resolving the un-cloned function.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

template<typename _It>
struct S
{
  _It _M_current;
  constexpr S (_It x) noexcept conveyor(auto) : _M_current (x) { }
};

S<int*> f (int* p) conveyor
{
  return S<int*> (p);
}

int
main ()
{
  int x = 42;
  return f (&x)._M_current == &x ? 0 : 1;
}
