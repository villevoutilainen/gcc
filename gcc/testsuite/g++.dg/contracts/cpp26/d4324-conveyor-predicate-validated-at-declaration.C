// D4324: a strict (conveyor) predicate's own restrictions are checked
// once, at declaration level, whether or not the contract is ever
// evaluated anywhere -- including for a function that is only declared,
// a pure virtual function, and a member function declared in a class.
// Previously they were checked only where the predicate was woven into a
// body (the definition, or a caller-side wrapper), so none of these were
// diagnosed.  Each is diagnosed exactly once.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

namespace sc = std::contracts;

int g;

int assigns (int x) pre<sc::conveyor_assert_v> ((x = 1) > 0);	// { dg-error "assignment to .x. not permitted in a conveyor predicate" }
int increments (int x) pre<sc::conveyor_assert_v> (++x > 0);	// { dg-error "increment of .x. not permitted in a conveyor predicate" }
// { dg-error "increment of .x. not provably free of overflow" "" { target *-*-* } .-1 }
int global (int x) pre<sc::conveyor_assert_v> (x > g);		// { dg-error "use of non-const variable .g. with static storage duration" }
int divides (int x) pre<sc::conveyor_assert_v> (10 / x > 1);	// { dg-error "divisor .x. not provably nonzero" }
int derefs (int* p) pre<sc::conveyor_assert_v> (*p > 0);	// { dg-error "pointer dereference of .p. not provably valid" }
int post_assigns (int& x) post<sc::conveyor_assert_v> (r: (x = r) > 0);	// { dg-error "assignment to .x. not permitted in a conveyor predicate" }

// Called, but still only declared.
int called (int x) pre<sc::conveyor_assert_v> ((x = 2) > 0);	// { dg-error "assignment to .x. not permitted in a conveyor predicate" }
int use_called () { return called (1); }

// Defined: diagnosed once, not again by the body walk.
int defined (int x) pre<sc::conveyor_assert_v> ((x = 3) > 0)	// { dg-error "assignment to .x. not permitted in a conveyor predicate" }
{ return x; }

struct B {
  virtual int f (int x) pre<sc::conveyor_assert_v> ((x = 4) > 0) = 0; // { dg-error "assignment to .x. not permitted in a conveyor predicate" }
  int m (int x) pre<sc::conveyor_assert_v> ((x = 5) > 0);	// { dg-error "assignment to .x. not permitted in a conveyor predicate" }
  virtual ~B () = default;
};

// Fine: an ordinary, side-effect-free, UB-free predicate.
int clean (int x, int y) pre<sc::conveyor_assert_v> (x < y && y != 0);
int guarded (int x) pre<sc::conveyor_assert_v> (x != 0 && 10 / x > 1);
int& ref_result (int& x) post<sc::conveyor_assert_v> (r: std::is_object_address (&r));

// A template is checked per instantiation, once its contracts are
// substituted; an uninstantiated or only-declared one isn't.
template <class T>
T tdefined (T x) pre<sc::conveyor_assert_v> ((x = T ()) > 0)	// { dg-error "assignment to .x. not permitted in a conveyor predicate" }
{ return x; }
int use_t () { return tdefined (1); }

template <class T>
T tnever (T x) pre<sc::conveyor_assert_v> ((x = T ()) > 0) { return x; }
