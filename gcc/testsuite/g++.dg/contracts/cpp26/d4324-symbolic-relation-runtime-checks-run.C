// D4324: -fcontract-symbolic-runtime-checks records relations.  One of
// objects only is recorded by the callee and lives as long as its objects;
// one with a value argument is recorded at the call site, keyed by the
// value's bytes (so a copy has it), and ends on every exit from the
// innermost block containing that call, an exception's included.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

int violations;
void handle_contract_violation (const contract_violation &) { ++violations; }

struct It { int *p; };
struct Vec { int d[4]; };
struct Node { int v; };
bool is_reachable (It, It) symbolic;
bool belongs_to (It, const Vec &) symbolic;
bool is_linked (const Node *, const Node *) symbolic;
bool lt (int, int) symbolic;

It vbegin (Vec &v) post<never_proven_symbolic_v>(r: belongs_to (r, v))
{ return It{v.d}; }
void range (Vec &v, It &f, It &l) post<never_proven_symbolic_v>(is_reachable (f, l))
{ f.p = v.d; l.p = v.d + 4; }
void order (const int a, const int b) post<never_proven_symbolic_v>(lt (a, b)) {}
void use_it (It i, const Vec &v) pre<symbolic_assert_v>(belongs_to (i, v)) {}
void use_range (It f, It l) pre<symbolic_assert_v>(is_reachable (f, l)) {}
void need_lt (int a, int b) pre<symbolic_assert_v>(lt (a, b)) {}
void link (Node *const a, Node *const b) post<never_proven_symbolic_v>(is_linked (a, b)) {}
void walk (const Node *a, const Node *b) pre<symbolic_assert_v>(is_linked (a, b)) {}

It kept;
void keep (Vec &v) { It i = vbegin (v); kept = i; use_it (i, v); }
void thrower (Vec &v) { It i = vbegin (v); kept = i; throw 1; }

#define EXPECT(n, stmt) \
  do { int before = violations; stmt; \
       if (violations - before != (n)) __builtin_abort (); } while (0)

int
main ()
{
  Vec v, w;
  It i = vbegin (v);
  EXPECT (0, use_it (i, v));
  It j = i;
  EXPECT (0, use_it (j, v));		// a copy of the value
  EXPECT (1, use_it (i, w));		// another object
  It f, l;
  range (v, f, l);
  EXPECT (0, use_range (f, l));
  EXPECT (1, use_range (l, f));		// in order
  order (1, 2);
  EXPECT (0, need_lt (1, 2));		// values of constants
  EXPECT (1, need_lt (2, 1));
  Node a, b;
  link (&a, &b);
  EXPECT (0, walk (&a, &b));
  EXPECT (1, walk (&b, &a));
  keep (v);
  EXPECT (1, use_it (kept, v));		// its block has ended
  {
    It k;
    {
      It x = vbegin (w);
      k = x;
      EXPECT (0, use_it (k, w));
    }
    EXPECT (1, use_it (k, w));		// likewise
  }
  try { thrower (v); } catch (int) {}
  EXPECT (1, use_it (kept, v));		// ended by the exception too
  for (int n = 0; n < 3; ++n)
    {
      It x = vbegin (v);
      EXPECT (0, use_it (x, v));
    }
}
