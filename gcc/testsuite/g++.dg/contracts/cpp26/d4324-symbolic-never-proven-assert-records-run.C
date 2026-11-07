// D4324: under -fcontract-symbolic-runtime-checks, a never_proven symbolic
// contract_assert is trusted, as the static analysis trusts it: it records
// its facts, rather than checking them, so the code it lets the analysis
// accept doesn't fail at run time.  A relation with a value argument it
// records ends with the assertion's block.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-runtime-checks -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
using namespace std::contracts;

int violations;
void handle_contract_violation (const contract_violation &) { ++violations; }

struct F { int fd; };
struct It { int *p; };
struct Node { int v; };
bool is_open (const F *) symbolic;
bool is_reachable (It, It) symbolic;
bool is_linked (const Node *, const Node *) symbolic;
void use (const F *f) pre<symbolic_assert_v>(is_open (f)) {}
void use_range (It f, It l) pre<symbolic_assert_v>(is_reachable (f, l)) {}
void walk (const Node *a, const Node *b) pre<symbolic_assert_v>(is_linked (a, b)) {}

struct Cursor
{
  It first, last;
  void run ()
  {
    contract_assert<never_proven_symbolic_v>(is_reachable (first, last));
    use_range (first, last);
  }
};

#define EXPECT(n, stmt) \
  do { int before = violations; stmt; \
       if (violations - before != (n)) __builtin_abort (); } while (0)

int
main ()
{
  F a, b;
  EXPECT (0, contract_assert<never_proven_symbolic_v>(is_open (&a)));
  EXPECT (0, use (&a));
  EXPECT (1, use (&b));
  Node x, y;
  contract_assert<never_proven_symbolic_v>(is_linked (&x, &y));
  EXPECT (0, walk (&x, &y));
  int buf[4];
  It f{buf}, l{buf + 4};
  {
    contract_assert<never_proven_symbolic_v>(is_reachable (f, l));
    EXPECT (0, use_range (f, l));
  }
  EXPECT (1, use_range (f, l));		// the assertion's block has ended
  Cursor c{f, l};
  EXPECT (0, c.run ());
  // A checking contract_assert still checks.
  EXPECT (1, contract_assert<symbolic_assert_v>(is_open (&b)));
}
