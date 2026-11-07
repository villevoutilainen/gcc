// D4324: a symbolic predicate relating values (by-value parameters of
// types with unique object representations) and objects: a copy of a
// value has its facts; they end when the variable holding it is written
// or a call may end an object it was obtained from (the derived-value
// rule, P4445 caller9).  The gimple engine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;

struct It { int *p; };
struct Vec { int d[4]; };
bool is_reachable (It, It) symbolic;
bool belongs_to (It, const Vec &) symbolic;
It vbegin (Vec &v) post<never_proven_symbolic_v>(r: belongs_to (r, v));
It vend (Vec &v);
void range (Vec &v, It &f, It &l) post<never_proven_symbolic_v>(is_reachable (f, l));
void crange (const It &f, const It &l) post<never_proven_symbolic_v>(is_reachable (f, l));
void vrange (const It f, const It l) post<never_proven_symbolic_v>(is_reachable (f, l));
void use_it (It i, const Vec &v) pre<symbolic_assert_v>(belongs_to (i, v));
void use_range (It f, It l) pre<symbolic_assert_v>(is_reachable (f, l));
void use_not (It f, It l) pre<symbolic_assert_v>(!is_reachable (f, l));
void grow (Vec &);
void growc (const Vec &);
void other ();

// Establishment: a result, through reference and value parameters.
void v1 (Vec &v) { It i = vbegin (v); use_it (i, v); }
void v2 (Vec &v) { It f, l; range (v, f, l); use_range (f, l); }
void v3 (It f, It l) { crange (f, l); use_range (f, l); }
void v4 (It f, It l) { vrange (f, l); use_range (f, l); }
void v5 (Vec &v, Vec &w) { It i = vbegin (v); use_it (i, w); }		// { dg-warning "cannot verify" }
void v6 (Vec &v) { It f, l; range (v, f, l); use_range (l, f); }	// { dg-warning "cannot verify" }
void v7 (Vec &v) { It f, l; range (v, f, l); use_not (f, l); }		// { dg-error "provably fails" }
void v8 (It f, It l) { contract_assert<never_proven_symbolic_v>(!is_reachable (f, l)); use_not (f, l); }

// A copy of a value has its facts.
void c1 (Vec &v) { It i = vbegin (v); It j = i; use_it (j, v); }
void c2 (Vec &v) { It f, l; range (v, f, l); It g = f; use_range (g, l); }
void c3 (Vec &v) { It i = vbegin (v); It j = i; j.p++; contract_assert<symbolic_assert_v>(belongs_to (i, v)); contract_assert<symbolic_assert_v>(belongs_to (j, v)); } // { dg-warning "cannot verify" }

void e1 (Vec &v) { use_it (vbegin (v), v); }
void c4 (Vec &v) { It j; It i = vbegin (v); j = i; use_it (j, v); }

// Writing the variable ends them.
void w1 (Vec &v, It g) { It f, l; range (v, f, l); f = g; use_range (f, l); }	// { dg-warning "cannot verify" }
void w2 (Vec &v) { It f, l; range (v, f, l); f.p = nullptr; use_range (f, l); }	// { dg-warning "cannot verify" }
void w3 (Vec &v) { It f, l; range (v, f, l); It *pf = &f; pf->p = nullptr; use_range (f, l); } // { dg-warning "cannot verify" }
// So does any call, once its address was handed out.
void w4 (Vec &v) { It f, l; range (v, f, l); other (); use_range (f, l); }	// { dg-warning "cannot verify" }

// The derived-value rule: a call that may end the object a value was
// obtained from ends its facts; one given no route to it doesn.t.
void d1 (Vec &v) { It f = vbegin (v), l = vend (v); contract_assert<never_proven_symbolic_v>(is_reachable (f, l)); use_range (f, l); }
void d2 (Vec &v) { It f = vbegin (v), l = vend (v); contract_assert<never_proven_symbolic_v>(is_reachable (f, l)); other (); use_range (f, l); }
void d3 (Vec &v) { It f = vbegin (v), l = vend (v); contract_assert<never_proven_symbolic_v>(is_reachable (f, l)); grow (v); use_range (f, l); } // { dg-warning "cannot verify" }
void d4 (Vec &v) { It f = vbegin (v), l = vend (v); contract_assert<never_proven_symbolic_v>(is_reachable (f, l)); growc (v); use_range (f, l); } // { dg-warning "cannot verify" }
void d5 (Vec &v) { It f = vbegin (v), l = vend (v); contract_assert<never_proven_symbolic_v>(is_reachable (f, l)); Vec w; grow (w); use_range (f, l); }
void d6 (Vec &v, Vec &u) { It f = vbegin (v), l = vend (v); contract_assert<never_proven_symbolic_v>(is_reachable (f, l)); grow (u); use_range (f, l); } // { dg-warning "cannot verify" }
void d7 (Vec &v) { Vec w; It f = vbegin (w), l = vend (w); contract_assert<never_proven_symbolic_v>(is_reachable (f, l)); grow (v); use_range (f, l); } // { dg-warning "cannot verify" }
void d8 (Vec &v) { It f = vbegin (v), l = vend (v); It g = f; contract_assert<never_proven_symbolic_v>(is_reachable (g, l)); grow (v); use_range (g, l); } // { dg-warning "cannot verify" }
// A parameter.s value may have been obtained from anything.
void d9 (It f, It l, Vec &v) { contract_assert<never_proven_symbolic_v>(is_reachable (f, l)); grow (v); use_range (f, l); } // { dg-warning "cannot verify" }
void d10 (It f, It l) { contract_assert<never_proven_symbolic_v>(is_reachable (f, l)); other (); use_range (f, l); }

// Joins.
void j1 (Vec &v, bool c) { It f, l; if (c) range (v, f, l); use_range (f, l); }	// { dg-warning "cannot verify" }
void j2 (Vec &v, bool c) { It f, l; if (c) range (v, f, l); else range (v, f, l); use_range (f, l); }
void j3 (It f, It l, int n) { contract_assert<never_proven_symbolic_v>(is_reachable (f, l)); for (int k = 0; k < n; ++k) use_range (f, l); }
void j4 (It f, It l, int n) { contract_assert<never_proven_symbolic_v>(is_reachable (f, l)); for (int k = 0; k < n; ++k) { use_range (f, l); f.p++; } } // { dg-warning "cannot verify" }
