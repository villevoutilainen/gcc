// gimple_object_address_plugin.cc: facts about persistent memory hold at
// a point only if they hold on every path to it; a call may change a
// local whose address was handed out before; a relation survives a copy;
// a predicate tested by a condition holds where it was true.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple -fcontract-analysis-proofs=plugin -fcontract-symbolic-proofs" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

struct trap_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context &ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr trap_ctrl trap_v{};

struct file
{
  static bool is_open (const file *) symbolic;
  void open () post<trap_v> (is_open (this));
  void close ();
  void read () pre<trap_v> (is_open (this));
};
void opaque ();

void merged (file &f, bool c)
{
  f.open ();
  if (c)
    f.close ();
  else
    f.close ();
  f.read ();		// { dg-warning "cannot verify that .static bool file::is_open" }
}

void one_arm (file &f, bool c)
{
  f.open ();
  if (c)
    opaque ();
  f.read ();
}

void escaped ()
{
  file f;
  f.open ();
  opaque ();
  f.read ();		// { dg-warning "cannot verify that .static bool file::is_open" }
}

void not_escaped ()
{
  file f;
  f.open ();
  f.read ();
}

struct It { int *p; };
struct Range { It first, last; };
bool reachable (It, It) symbolic;
Range whole () post<trap_v> (r: reachable (r.first, r.last));
void walk (It a, It b) pre<trap_v> (reachable (a, b));

void copied () { Range r = whole (); Range q = r; walk (q.first, q.last); }
void swapped () { Range r = whole (); walk (r.last, r.first); } // { dg-warning "cannot verify that .bool reachable" }

struct P { bool on; bool ok () const conveyor { return on; } };
void need_ok (const P &p) pre<trap_v> (p.ok ());
void tested (const P &p) { if (p.ok ()) need_ok (p); }	// { dg-error "cannot prove .is_object_address." }
void untested (const P &p) { need_ok (p); }	// { dg-warning "cannot verify that .bool P::ok" }
