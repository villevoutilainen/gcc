// D4324: a contract condition written in a macro is reported with the
// condition's own text, spelled from its tokens, not with the macro's name
// (P4445 BUG-36).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=observe" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
#include <cstring>

namespace sc = std::contracts;

const char* seen[8];
int n;

struct grab {
  static constexpr bool is_ignored (sc::assertion_static_info) { return false; }
  void
  operator() (const sc::assertion_context& ctx) const
  { if (!ctx.check ()) seen[n++] = ctx.comment (); }
};

#define POSITIVE(v) pre<grab{}>((v) > 0)
#define PRE_LT(a, b) pre<grab{}>(a < b)
#define NONZERO(e) contract_assert<grab{}>(e != 0)
#define LIMIT 10
#define MAX(a, b) ((a) > (b) ? (a) : (b))

int f (int x) POSITIVE (x) { return x; }
int g (int x, int y) PRE_LT (x, y + 1) { return x; }
int h (int x) pre<grab{}>(x < LIMIT) { return x; }	// macro at the end
int k (int x) { NONZERO (x); return x; }
int m (int x) pre<grab{}>(x < MAX (1, 2)) { return x; }	// function-like

struct S
{
  int v;
  int m (int i) PRE_LT (i, v) { return i; }		// member: parsed late
};

int main ()
{
  f (-1);
  g (5, 1);
  h (20);
  k (0);
  m (5);
  S s { 1 };
  s.m (3);
  const char* want[] = { "(x) > 0", "x < y + 1", "x < LIMIT", "x != 0",
			 "x < MAX (1, 2)", "i < v" };
  if (n != 6)
    __builtin_abort ();
  for (int i = 0; i < n; ++i)
    if (std::strcmp (seen[i], want[i]) != 0)
      __builtin_abort ();
  return 0;
}
