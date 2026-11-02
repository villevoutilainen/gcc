// D4324: a call given a local's address, or a reference or pointer
// copied from it, may end the local (the lifetime rule), as in the AST
// engine; a trust after the call makes it an object again.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
#define ALIVE(x) contract_assert<std::contracts::never_proven_conveyor_v>(std::is_object_address(__builtin_addressof(x)))
#define CA std::contracts::conveyor_assert_v
struct M { int v; int lb(int) const; int cb() const conveyor pre<CA>(std::is_object_address(this)) { return v; } };
int f1() { M m{1}; const auto& cm = m; int a = cm.lb(0); ALIVE(cm); return a + cm.cb(); }
int f2() { M m{1}; int a = m.lb(0); ALIVE(m); return a + m.cb(); }
int f3() { M m{1}; const auto& cm = m; int a = cm.lb(0); return a + cm.cb(); } // { dg-error "cannot prove .is_object_address." }
