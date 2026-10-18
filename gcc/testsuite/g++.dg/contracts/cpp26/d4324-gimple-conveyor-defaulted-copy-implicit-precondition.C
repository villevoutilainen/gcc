// D4324: a defaulted copy or move constructor or assignment operator whose
// body passes its source on to a copy or move requiring is_object_address
// of it is treated as if it had that precondition itself -- its body
// trusts the source, its callers must prove it -- without being given one,
// so it stays as trivial as it is.  One whose body needs nothing of its
// source owes nothing.
// The GIMPLE engine's version.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
struct A { int v; A () : v (0) {} A (const A& o) pre<std::contracts::never_proven_conveyor_v> (std::is_object_address (&o)) : v (o.v) {} };
struct Plain { int v; };
struct H { A a; H () = default; H (const H&) = default; };
struct HH { H h; HH () = default; HH (const HH&) = default; };
struct P { Plain p; P () = default; P (const P&) = default; };
H h_ok () { H h; H c (h); return c; }
H h_bad (const H& r) { return H (r); }		// { dg-error "cannot prove .is_object_address. for \[^\n\]*required by the implicit precondition" }
HH hh_bad (const HH& r) { return HH (r); }	// { dg-error "cannot prove .is_object_address. for \[^\n\]*required by the implicit precondition" }
HH hh_ok () { HH h; return HH (h); }
P p_ok (const P& r) { return P (r); }
