// D4324: a never_proven assertion of any kind needs no proof of its input
// facts, so a caller owes nothing for a never_proven precondition -- nor
// for the implicit precondition of a reference parameter or 'this' that one
// already claims is an object.  That is what makes std::forward conveyor:
// forwarded arguments, parameter packs included, flow through conveyor
// functions.  A conveyor function's parameter pack gets the implicit
// precondition too, one conjunct per parameter.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -D_GLIBCXX_CONVEYOR_ASSERTIONS -fcontract-analysis-engine=ast" }

#include <utility>
#include <contracts>
using std::contracts::never_proven_conveyor_v;

int val (int x) conveyor pre<never_proven_conveyor_v> (x > 0) { return x; }
int ex (int &r) conveyor
  pre<never_proven_conveyor_v> (std::is_object_address (&r))
{ return r; }
struct S
{
  int v;
  int get () const conveyor
    pre<never_proven_conveyor_v> (std::is_object_address (this))
  { return v; }
};
int im (int &r) conveyor { return r; }

int u0 (int x) conveyor { return val (x); }
int u1 (int &x) { return ex (x); }
int u2 (S *s) { return s->get (); }
int u3 (int &x) { return im (x); }	// { dg-error "cannot prove .is_object_address." }

// Forwarding, and forwarded parameter packs.
template<class T> bool one (T &&t) conveyor { return sizeof (t) > 0; }
template<class T> bool fwd1 (T &&t) conveyor { return one (std::forward<T> (t)); }
template<class... Ts> bool all (Ts &&... ts) conveyor
{ return (one (std::forward<Ts> (ts)) && ...); }
template<class... Ts> bool pass (Ts &&... ts) conveyor
{ return all (std::forward<Ts> (ts)...); }
template<class... Ts> bool direct (Ts &&... ts) conveyor
{ return (one (ts) || ...); }
struct M { int v; };
template<class T> int like (T &&t) conveyor { return std::forward_like<T> (t.v); }

bool
use (int x, const double d, M m)
{
  return fwd1 (x) && all (x, d, 3) && pass (x, d, 3, 'c') && direct (x, 2)
	 && like (m) + like (std::move (m)) && all ();
}

// A pack's arguments are owed like any other reference parameter's.
bool
owes (int &y)
{
  return pass (y, 1);	// { dg-error "cannot prove .is_object_address." }
}
