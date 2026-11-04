// D4324 (GIMPLE engine): in a compiler-synthesized memberwise copy or move (a defaulted or
// implicitly defined copy or move constructor or assignment operator), a
// member's copy or move doesn't end the facts of the source's other
// members, nor -- in an assignment -- of the destination's, although the
// class is standard-layout (where a route to a member reaches its whole
// object): the compiler wrote the body, and nobody could trust the later
// members in it.  User-written code doing the same is still diagnosed.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

// Copies and moves that aren't conveyor: given a route into the source.
struct NC
{
  int v;
  NC () : v (0) {}
  NC (const NC& o) : v (o.v) {}
  NC (NC&& o) noexcept : v (o.v) { o.v = 0; }
  NC& operator= (const NC& o) { v = o.v; return *this; }
  NC& operator= (NC&& o) noexcept { v = o.v; o.v = 0; return *this; }
};

// Copies and moves that need their source to be an object.
struct H
{
  int v;
  H () : v (0) {}
  H (const H& o) pre<std::contracts::conveyor_assert_v> (std::is_object_address (&o)) : v (o.v) {}
  H (H&& o) noexcept pre<std::contracts::conveyor_assert_v> (std::is_object_address (&o)) : v (o.v) {}
  H& operator= (const H& o) pre<std::contracts::conveyor_assert_v> (std::is_object_address (&o))
  { v = o.v; return *this; }
  H& operator= (H&& o) noexcept pre<std::contracts::conveyor_assert_v> (std::is_object_address (&o))
  { v = o.v; return *this; }
};

struct S { NC a; H b; };			// implicit copy/move/assign
struct D { NC a; H b; D () = default; D (const D&) = default;
	   D (D&&) = default; D& operator= (const D&) = default;
	   D& operator= (D&&) = default; };	// defaulted

// Callers prove the source and destination (locals).
S s_copy () { S s; return S (s); }
S s_move () { S s; return S (static_cast<S&&> (s)); }
void s_copy_assign () { S d, s; d = s; }
void s_move_assign () { S d, s; d = static_cast<S&&> (s); }
D d_copy () { D s; return D (s); }
D d_move () { D s; return D (static_cast<D&&> (s)); }
void d_copy_assign () { D d, s; d = s; }
void d_move_assign () { D d, s; d = static_cast<D&&> (s); }

// The same member moves written by hand: the first ends the source (a
// standard-layout class), so the second can't be proven.
H by_hand ()
{
  S s;
  NC a (static_cast<NC&&> (s.a));
  return H (static_cast<H&&> (s.b));	// { dg-error "cannot prove .is_object_address." }
}

// The defaulted assignment trusts its destination; its caller proves it.
void dest_unproven (S& d) { S s; d = s; }	// { dg-error "cannot prove .is_object_address. for \[^\n\]*required by the implicit precondition" }
