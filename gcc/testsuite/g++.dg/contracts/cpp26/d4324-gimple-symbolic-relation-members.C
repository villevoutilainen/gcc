// D4324: a relation's value argument may be a member of a variable
// ('reachable_from (r.first, r.last)' about a returned range): a fact
// about that member's value, which ends whenever the variable's facts do;
// a copy of the variable, or of the member, has it, as does a structured
// binding of it.  The gimple engine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;

struct It { int *p; };
struct Range { It first, last; };
struct Vec { int d[4]; };
bool reachable_from (It, It) symbolic;
Range whole (Vec &v) post<never_proven_symbolic_v>(r: reachable_from (r.first, r.last));
void sum (It a, It b) pre<symbolic_assert_v>(reachable_from (a, b));
void grow (Vec &);
void other ();

void m1 (Vec &v) { Range r = whole (v); sum (r.first, r.last); }
void m2 (Vec &v) { Range r = whole (v); sum (r.last, r.first); }		// { dg-warning "cannot verify" }
void m3 (Vec &v) { Range r = whole (v); Range q = r; sum (q.first, q.last); }
void m4 (Vec &v) { Range r = whole (v); It a = r.first; sum (a, r.last); }
void m5 (Vec &v, It x) { Range r = whole (v); r.first = x; sum (r.first, r.last); } // { dg-warning "cannot verify" }
void m6 (Vec &v, Range x) { Range r = whole (v); r = x; sum (r.first, r.last); }	// { dg-warning "cannot verify" }
void m7 (Vec &v) { Range r = whole (v); grow (v); sum (r.first, r.last); }	// { dg-warning "cannot verify" }
void m8 (Vec &v) { Range r = whole (v); other (); sum (r.first, r.last); }
void m9 (Vec &v) { auto [f, l] = whole (v); sum (f, l); }
void m10 (Range r) pre<symbolic_assert_v>(reachable_from (r.first, r.last)) { sum (r.first, r.last); }
void m11 (Range r) { contract_assert<never_proven_symbolic_v>(reachable_from (r.first, r.last)); sum (r.first, r.last); }
void m12 (Vec &v, Vec &w) { Range r = whole (v), s = whole (w); sum (r.first, s.last); }	// { dg-warning "cannot verify" }
void m13 (Vec &v) { Range r = whole (v); It a = r.first, b = r.last; sum (a, b); }
void m14 (Vec &v) { Range r = whole (v); It a = r.first, b = r.last; grow (v); sum (a, b); }	// { dg-warning "cannot verify" }
void m15 (Vec &v) { Range r = whole (v); It a = r.first, b = r.last; other (); sum (a, b); }
