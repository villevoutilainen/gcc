// D4324/P2680 item 8's overflow scan: a precondition's own left-to-right,
// short-circuit '&&' evaluation order is now applied when scanning it --
// 'x < 100000' (the first conjunct) has already evaluated true by the
// time 'x++' (the second) runs, bounding x well below TYPE_MAX. The
// motivating real-world report: https://godbolt.org/z/vjfxK7Psz.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

void f (int x)
pre<std::contracts::conveyor_assert_v>(x < 100000 && x++ < 2048)
{}

int main () { f (1); return 0; }
