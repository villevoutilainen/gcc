// D4324: a template whose contract names a DEPENDENT control object
// ('pre<Policy::label> (...)') can't tell, as a pattern, whether that
// contract is conveyor-active, so the pattern gets no implicit
// is_object_address precondition on its reference parameters and 'this'.
// Each instantiation can tell: one whose label is conveyor-like gets them
// (callers must prove them, the body may rely on them); one whose label
// isn't gets none.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>

struct Conveyor { static constexpr auto &label = std::contracts::conveyor_assert_v; };
struct Ordinary { static constexpr auto &label = std::contracts::default_v; };

// The body reads X: fine only with the implicit precondition.
template <class Pol>
int f (const int &x) pre<Pol::label> (true) { return x; }

template <class Pol>
struct S
{
  int v;
  int g () pre<Pol::label> (true) { return v; }
};

template <class Pol, class T>
void h (const T &) pre<Pol::label> (true) {}

void
bad (int &a, S<Conveyor> &s)
{
  f<Conveyor> (a);	// { dg-error "cannot prove" }
  s.g ();		// { dg-error "cannot prove" }
  h<Conveyor> (a);	// { dg-error "cannot prove" }
}

void
ordinary (int &a, S<Ordinary> &s)
{
  f<Ordinary> (a);
  s.g ();
  h<Ordinary> (a);
}

void
good ()
{
  int i = 0;
  S<Conveyor> s{};
  f<Conveyor> (i);
  s.g ();
  h<Conveyor> (i);
}
