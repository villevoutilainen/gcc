// D4324/P2680 item 8's overflow scan: a precondition's own left-to-right,
// short-circuit '&&' evaluation order is now applied when scanning it --
// 'x < 100000' (the first conjunct) has already evaluated true by the
// time 'x + x' (the second) runs, bounding x well below TYPE_MAX. The
// motivating real-world report: https://godbolt.org/z/vjfxK7Psz.
//
// Uses 'x + x', not the original 'x++': a direct mutation of X (a
// received, non-owned parameter) inside conveyor-flavored condition
// text is now a separate, unconditional violation regardless of its
// own overflow-safety (see .claude/plans/lazy-stirring-pearl.md,
// 2026-09-02) -- 'x + x' still exercises the identical left-to-right
// refinement and general-binary-arithmetic overflow proof this test
// exists to cover, without mutating anything, so it stays a clean -ok
// test of exactly that.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

void f (int x)
pre<std::contracts::conveyor_assert_v>(x < 100000 && x + x < 2048)
{}

int main () { f (1); return 0; }
