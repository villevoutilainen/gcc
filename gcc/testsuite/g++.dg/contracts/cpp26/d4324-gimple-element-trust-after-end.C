// D4324: a trust of an element after a call given the whole array (which
// may have ended it) holds for that element -- not for another element,
// and not past a further such call -- in a loop too.  The same in both
// engines.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>
#define TRUST(x) contract_assert<std::contracts::never_proven_conveyor_v>(std::is_object_address(x))
#define CA std::contracts::conveyor_assert_v
struct E { int v; int get() const pre<CA>(std::is_object_address(this)) { return v; } };
void touch(const E (&)[4]);
int ok(const E (&a)[4], int i) { touch(a); TRUST(&a[i]); return a[i].get(); }
int ended_again(const E (&a)[4], int i) { touch(a); TRUST(&a[i]); touch(a); return a[i].get(); } // { dg-error "cannot prove .is_object_address." }
int other_elt(const E (&a)[4], int i, int j) { touch(a); TRUST(&a[i]); return a[j].get(); } // { dg-error "cannot prove .is_object_address." }
int loop(const E (&a)[4]) { int s = 0; for (int i = 0; i < 4; ++i) { TRUST(&a[i]); s += a[i].get(); } return s; }
