// D4324: a symbolic predicate of one by-value class argument with unique
// object representations ('is_zero_terminated (std::string_view)') is a
// fact about the value, as a relation's value arguments are: a copy has
// it; it ends when the variable holding it is written or a call may end
// an object it was obtained from.  The gimple engine.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs -fcontract-analysis-engine=gimple" }

#include <contracts>
using namespace std::contracts;

struct View					// as std::string_view
{
  const char *p;
  unsigned long n;
  void remove_prefix (unsigned long k) { p += k; n -= k; }
};

bool is_zero_terminated (View) symbolic;
View make_s () post<never_proven_symbolic_v>(r: is_zero_terminated (r));
View from (const char *p) post<never_proven_symbolic_v>(r: is_zero_terminated (r));
int read_s (View sv) pre<symbolic_assert_v>(is_zero_terminated (sv));
int read_n (View sv) pre<symbolic_assert_v>(!is_zero_terminated (sv));
void other ();
void grow (char *);

int e1 () { return read_s (make_s ()); }
int e2 () { View s = make_s (); return read_s (s); }
// A copy of a value has its facts.
int c1 () { View s = make_s (); View t = s; return read_s (t); }
int c2 () { View s = make_s (), t; t = s; return read_s (t); }
// Writing it ends them.
int w1 (View u) { View s = make_s (); s = u; return read_s (s); } // { dg-warning "cannot verify" }
int w2 () { View s = make_s (); s.remove_prefix (1); return read_s (s); } // { dg-warning "cannot verify" }
// The derived-value rule.
int d1 (char *p) { View s = from (p); other (); return read_s (s); }
int d2 (char *p) { View s = from (p); grow (p); return read_s (s); } // { dg-warning "cannot verify" }
// Own precondition; contract_assert; polarity.
int s1 (View sv) pre<symbolic_assert_v>(is_zero_terminated (sv)) { return read_s (sv); }
int s2 (View sv) { contract_assert<never_proven_symbolic_v>(is_zero_terminated (sv)); return read_s (sv); }
int s3 (View sv) { contract_assert<never_proven_symbolic_v>(is_zero_terminated (sv)); return read_n (sv); } // { dg-error "provably fails" }
int s4 (View sv) { return read_s (sv); } // { dg-warning "cannot verify" }
