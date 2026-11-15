// D4324 (GIMPLE engine): a call given any route to an object -- a member
// call's 'this', a const reference -- may keep it, and a later call write
// through it (P4445 BUG-17).
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-symbolic-proofs -fcontract-analysis-engine=gimple" }

#include <contracts>

int quot (int a, int b) pre<std::contracts::proven_conveyor_v> (b != 0);
void clobber ();			// opaque

struct S { int v; void keep (); int get () const; };
void keep_c (const S &);
struct T { mutable int v; };
void keep_t (const T &);

// The address escapes first, the fact comes after, and an unrelated call
// may write through the kept address.
int this_escape () { S s; s.keep (); s.v = 5; clobber (); return quot (10, s.v); } // { dg-error "cannot prove that .s.S::v. is nonzero" }
int const_this_escape () { S s; (void) s.get (); s.v = 5; clobber (); return quot (10, s.v); } // { dg-error "cannot prove that .s.S::v. is nonzero" }
int const_ref_escape () { S s; keep_c (s); s.v = 5; clobber (); return quot (10, s.v); } // { dg-error "cannot prove that .s.S::v. is nonzero" }
int mutable_escape () { T t; keep_t (t); t.v = 5; clobber (); return quot (10, t.v); } // { dg-error "cannot prove that .t.T::v. is nonzero" }

// A symbolic fact too: what the earlier call kept, the later one may use
// to change the object.
struct symbolic_ctrl {
  static constexpr bool is_symbolic (std::contracts::assertion_static_info) { return true; }
  void operator() (const std::contracts::assertion_context &ctx) const
  { if (!ctx.check ()) __builtin_trap (); }
};
inline constexpr symbolic_ctrl sym_v{};
struct file {
  static bool is_open (const file *) symbolic;
  void open () post<sym_v> (is_open (this));
  void read () pre<sym_v> (is_open (this));
};
void sym_escape () { file f; f.open (); clobber (); f.read (); } // { dg-warning "cannot verify" }
void sym_no_call () { file f; f.open (); f.read (); }
