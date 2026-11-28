// symbolic_proof_plugin.cc under -fcontract-analysis-proofs=plugin: the
// plugin's own facts -- a predicate through a pointer held in a member,
// what a call may change, a copy versus an elided result, a relation of
// iterators and what may invalidate it.
// { dg-do compile }
// { dg-options "-std=c++26 -fcontracts -fcontract-control-objects -fcontract-analysis-proofs=plugin -fcontract-symbolic-proofs" }
// { dg-skip-if "requires hosted libstdc++ for stdc++exp" { ! hostedlib } }

#include <contracts>
namespace sc = std::contracts;

struct sym_ctrl {
  static constexpr bool is_symbolic (sc::assertion_static_info) { return true; }
  void operator() (const sc::assertion_context &ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr sym_ctrl sym_v{};

struct file { bool opened = false; };
bool is_opened (const file *) symbolic;
void open_it (file * const f) post<sym_v> (is_opened (f));
void use_it (file *f) pre<sym_v> (is_opened (f));
void mutate (file *);
struct holder { file *ptr; int count; };

void slot ()
{
  file f;
  holder h;
  h.ptr = &f;
  open_it (h.ptr);
  h.count = 1;
  use_it (&f);
}

void changed (file *a)
{
  open_it (a);
  mutate (a);
  use_it (a);			// { dg-warning "cannot verify that .bool is_opened" }
}

struct T { int v; };
bool good (const T &) symbolic;
T make () post<sym_v> (r: good (r));
int by_val (T t) pre<sym_v> (good (t));
int elided () { return by_val (make ()); }
int copied () { T s = make (); return by_val (s); } // { dg-warning "cannot verify that .bool good" }

struct It { int *p; };
struct Vec { int d[4]; };
bool reachable (It, It) symbolic;
void range (Vec &v, It &f, It &l) post<sym_v> (reachable (f, l));
void walk (It f, It l) pre<sym_v> (reachable (f, l));
void grow (Vec &);
void other ();

// f and l escaped to range by reference, so other () may change them.
void kept (Vec &v) { It f, l; range (v, f, l); other (); walk (f, l); } // { dg-warning "cannot verify that .bool reachable" }
void swapped (Vec &v) { It f, l; range (v, f, l); walk (l, f); } // { dg-warning "cannot verify that .bool reachable" }
void grown (Vec &v) { It f, l; range (v, f, l); grow (v); walk (f, l); } // { dg-warning "cannot verify that .bool reachable" }
