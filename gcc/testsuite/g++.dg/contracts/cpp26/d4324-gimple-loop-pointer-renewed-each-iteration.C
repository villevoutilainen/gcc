// D4324: a call that ends what a pointer in a loop points to doesn't end
// the object the next iteration's pointer, a new value of the same
// variable, points to.  The same in both engines.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
#define TRUST(x) contract_assert<std::contracts::never_proven_conveyor_v>(std::is_object_address(__builtin_addressof(x)))
#define CA std::contracts::conveyor_assert_v
struct P { int v; int get() const conveyor pre<CA>(std::is_object_address(this)) { return v; } };
P* make();
void destroy(P*);
int ok_loop(int n) { int s = 0; for (int i = 0; i < n; ++i) { P* p = make(); TRUST(*p); s += p->get(); destroy(p); } return s; }
int bad_loop(int n) { int s = 0; for (int i = 0; i < n; ++i) { P* p = make(); TRUST(*p); destroy(p); s += p->get(); } return s; } // { dg-error "cannot prove .is_object_address." }
