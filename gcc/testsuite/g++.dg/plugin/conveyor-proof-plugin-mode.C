// conveyor_proof_plugin.cc under -fcontract-analysis-proofs=plugin: the
// engine makes no proof checks; the plugin's own facts do -- a loop's
// fixed point, a relation and a shift, a write through an alias, a
// function's own postcondition, a contract whose control object forces
// analysis.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-analysis-proofs=plugin -fcontract-conveyor-proofs" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

struct trap_ctrl {
  static constexpr bool is_conveyor (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context &ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr trap_ctrl trap_v{};

int pos (int x) pre<trap_v> (x > 0) post<trap_v> (r: r > 1);
struct S { int size () const conveyor { return 5; } };
int at (const S &s, int i) pre<trap_v> (i >= 0 && i < s.size ());

int loop (int n)
{
  int s = 0;
  for (int i = 1; i < n; ++i)
    s += pos (i);
  for (int i = 0; i < n; ++i)
    s += pos (i);		// { dg-warning "cannot verify that .i. satisfies" }
  return s;
}

int shifted (const S &s, int i, int k)
  pre<trap_v> (i >= 0 && i < s.size () && k <= 0 && k > -1000 && i + k >= 0)
{
  int j = i + k;
  return at (s, j);
}

void alias (int k)
{
  if (k > 5)
    {
      int *p = &k;
      *p = 1;			// the write through p ends k > 5
      pos (k);			// { dg-warning "cannot verify that .k. satisfies" }
      int q = k;
      pos (q);			// { dg-warning "cannot verify that .q. satisfies" }
    }
}

int own_post (int a) post<trap_v> (r: r > 0)
{
  if (a > 0)
    return a;
  return 1;
}

int own_post_bad (int a) post<trap_v> (r: r > 0) // { dg-warning "cannot verify postcondition condition" }
{
  return a;
}

void forced (int x) pre<sc::proven_conveyor_v> (x > 10);
void use_forced (int x) { forced (x); } // { dg-error "cannot prove that .x. satisfies" }
