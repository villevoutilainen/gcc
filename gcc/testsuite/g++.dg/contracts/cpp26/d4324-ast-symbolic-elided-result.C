// D4324: a callee's postcondition about its result is a fact about the
// object that result initializes -- with guaranteed elision, a parameter
// passed by value, or the temporary a reference parameter is bound to.
// A copy, trivial or not, is a new object, with none of the original's
// facts.  The ast engine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs -fcontract-analysis-engine=ast" }

#include <contracts>
using namespace std::contracts;

struct T { int v; };						// trivially copyable
struct N { int v; N (int x) : v (x) {} N (const N &o) : v (o.v) {} };
bool good (const T &) symbolic;
bool good (const N &) symbolic;
T make_t () post<never_proven_symbolic_v>(r: good (r));
N make_n () post<never_proven_symbolic_v>(r: good (r));
int by_val_t (T t) pre<symbolic_assert_v>(good (t));
int by_val_n (N n) pre<symbolic_assert_v>(good (n));
int by_ref_t (const T &t) pre<symbolic_assert_v>(good (t));
int by_ref_n (const N &n) pre<symbolic_assert_v>(good (n));

int elided_t () { return by_val_t (make_t ()); }
int elided_n () { return by_val_n (make_n ()); }
int bound_t () { return by_ref_t (make_t ()); }
int named_t () { T s = make_t (); return by_ref_t (s); }

int copied_t () { T s = make_t (); return by_val_t (s); } // { dg-warning "cannot verify" }
int copied_n () { N s = make_n (); return by_val_n (s); } // { dg-warning "cannot verify" }
int named_copy_t () { T s = make_t (); T c = s; return by_ref_t (c); } // { dg-warning "cannot verify" }
