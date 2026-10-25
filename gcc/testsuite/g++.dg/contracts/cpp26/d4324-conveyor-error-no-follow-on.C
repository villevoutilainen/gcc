// D4324: a conveyor violation rejects the program but leaves the code valid
// C++, so the expression is still built: no follow-on errors from an
// invalid deduced return type or a constraint that turned false.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int plain (int x) { return x; }	// not conveyor

template<class T>
auto g (T x) conveyor { return plain (x); }	// { dg-error "call to .int plain.int.., which is not declared .conveyor." }

template<class T>
concept CanG = requires (T t) { g (t); };

static_assert (CanG<int>);
static_assert (__is_same (decltype (g (1)), int));

template<class T>
auto r (T *p) conveyor { return reinterpret_cast<long *> (p); }	// { dg-error ".reinterpret_cast. not permitted in a conveyor function or predicate" }

static_assert (__is_same (decltype (r ((int *) nullptr)), long *));

// The rest of a rejected construct isn't reported again: not the
// new-expression's operator new/delete calls, nor the destructor call an
// explicit destructor call makes, nor the result's use in the analysis.
int fresh () conveyor { return *new int (1); }	// { dg-error ".new.-expression not permitted" }

struct S { ~S () {} };
void destroy (S &s) conveyor { s.~S (); }	// { dg-error "explicit destructor call not permitted" }

// A violation in a template is reported once, not per instantiation.
template<class T>
T twice (T x) conveyor { return plain (x); }	// { dg-error "call to .int plain.int.., which is not declared .conveyor." }
int use_twice () { return twice (1) + twice (2L); }

// In a contract condition the rejected condition is dropped instead: it
// has no other consequences, and nothing more is reported about it.
struct M { int f (int x) const { return x; } };
int
pm1 (const M *s, int (M::*pmf) (int) const, int x)
  pre<std::contracts::conveyor_assert_v> ((s->*pmf) (x) > 0)	// { dg-error "call through a function pointer" }
{
  return x;
}
int
pm2 (const M *s, int (M::*pmf) (int) const, int x)
{
  contract_assert<std::contracts::conveyor_assert_v> ((s->*pmf) (x) > 0);	// { dg-error "call through a function pointer" }
  return x;
}
