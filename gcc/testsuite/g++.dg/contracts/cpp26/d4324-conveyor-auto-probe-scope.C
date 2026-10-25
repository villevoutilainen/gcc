// D4324: a conveyor(auto) deduction absorbs only the violations in the
// body of the specialization being deduced -- they just mean it isn't
// conveyor.  A violation in another body instantiated meanwhile, or in a
// conveyor contract condition (whose flavor comes from its control object),
// is an error.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

int plain (int x) { return x; }	// not conveyor

// Absorbed: f<int> is simply not conveyor.
template<class T> int f (T x) conveyor(auto) { return plain (x); }
template<class T> int ok (T x) conveyor(auto) { return x; }
int user (int x) { return f (x); }
int cv (int x) conveyor { return ok (x); }
int bad (int x) conveyor { return f (x); }	// { dg-error "call to .int f.T. .with T = int.., which is not declared .conveyor." }

// Not absorbed: h is declared conveyor, and its body is instantiated (for
// its deduced return type) while f2<int> is being deduced.
template<class T> auto h (T x) conveyor { return plain (x); }	// { dg-error "call to .int plain.int.., which is not declared .conveyor." }
template<class T> int f2 (T x) conveyor(auto) { return h (x); }
int user2 (int x) { return f2 (x); }

// Not absorbed: a conveyor assertion's condition.
template<class T> int f3 (T x) conveyor(auto)
{
  contract_assert<std::contracts::conveyor_assert_v> (plain (x) > 0);	// { dg-error "call to .int plain.int.., which is not declared .conveyor." }
  return x;
}
int user3 (int x) { return f3 (x); }
