// D4324: symbolic relations: self-trust of a precondition, a trusted
// postcondition, scalar and constant values, and the object a member
// function is called on.  The gimple engine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;

struct It { int *p; };
struct Vec {
  int d[4];
  It begin () post<never_proven_symbolic_v>(r: owns_it (*this, r));
  void use (It i) pre<symbolic_assert_v>(owns_it (*this, i));
  bool owns (It) const symbolic;
  It mbegin () post<never_proven_symbolic_v>(r: owns (r));
  void muse (It i) pre<symbolic_assert_v>(owns (i));
  static bool owns_it (const Vec &, It) symbolic;
};
bool is_reachable (It, It) symbolic;
bool index_valid (const Vec &, unsigned long) symbolic;
bool lt (int, int) symbolic;
void use_range (It f, It l) pre<symbolic_assert_v>(is_reachable (f, l));
int at (const Vec &v, unsigned long i) pre<symbolic_assert_v>(index_valid (v, i));
unsigned long find (const Vec &v) post<never_proven_symbolic_v>(r: index_valid (v, r));
void need_lt (int a, int b) pre<symbolic_assert_v>(lt (a, b));
void grow (Vec &);

// A function's own precondition holds in its body.
void s1 (It f, It l) pre<symbolic_assert_v>(is_reachable (f, l)) { use_range (f, l); }
void s2 (It f, It l) pre<symbolic_assert_v>(is_reachable (f, l)) { It g = l; use_range (f, g); }
// A relation in a defined function's postcondition is a trusted axiom.
It s3 (Vec &v) post<symbolic_assert_v>(r: Vec::owns_it (v, r)) { return v.begin (); }
// An object and a scalar value.
int s4 (const Vec &v) { unsigned long i = find (v); return at (v, i); }
int s5 (Vec &v) { unsigned long i = find (v); grow (v); return at (v, i); } // { dg-warning "cannot verify" }
int s6 (const Vec &v) { unsigned long i = find (v); unsigned long j = i; return at (v, j); }
int s7 (const Vec &v) { unsigned long i = find (v); return at (v, i + 1); } // { dg-warning "cannot verify" }
// Constants.
void s8 () { contract_assert<never_proven_symbolic_v>(lt (1, 2)); need_lt (1, 2); }
void s9 (int a) { contract_assert<never_proven_symbolic_v>(lt (a, 2)); need_lt (a, 2); }
void s10 (int a) { contract_assert<never_proven_symbolic_v>(lt (a, 2)); need_lt (2, a); } // { dg-warning "cannot verify" }
// The object a member function is called on.
void s11 (Vec &v) { It i = v.begin (); v.use (i); }
void s12 (Vec &v, Vec &w) { It i = v.begin (); w.use (i); }	// { dg-warning "cannot verify" }
void s13 (Vec &v) { It i = v.mbegin (); v.muse (i); }
void s14 (Vec &v, Vec &w) { It i = v.mbegin (); w.muse (i); }	// { dg-warning "cannot verify" }
