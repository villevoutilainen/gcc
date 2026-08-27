// D4324/P2680 item 8's overflow scan: the reference-parameter companion
// to d4324-conveyor-overflow-precondition-conjunct-order-ok.C -- 'x <
// 100000' must bound X for the second conjunct's 'x++' just as well
// whether X is a value or a reference parameter (reading through a
// reference is transparent; nothing about the safety argument depends
// on it). Previously a false positive ("not provably free of overflow")
// for the reference case only: the whole family of scalar/relational
// conjunct matchers required their operand to already be a bare PARM_
// DECL/VAR_DECL, but reading a REFERENCE_TYPE parameter's value produces
// INDIRECT_REF(x), not bare x, so no fact was ever established or
// consulted for it at all. Found via direct testing (Ville, Godbolt).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

void f (int& x)
pre<std::contracts::conveyor_assert_v>(x < 100000 && x++ < 2048)
{}

int main () { int v = 1; f (v); return 0; }
