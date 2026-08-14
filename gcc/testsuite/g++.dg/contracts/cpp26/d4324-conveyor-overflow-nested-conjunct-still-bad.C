// D4324/P2680 item 8's overflow scan: a '&&' nested one level *inside*
// another expression shape -- here, a call argument used as an 'if'
// condition -- is not decomposed into conjuncts at all (oa_collect_
// conjuncts only ever looks at its own argument's top level), so it
// stays conservatively rejected. This is a pre-existing limitation of
// oa_process_condition itself, confirmed via direct testing to already
// reject this exact shape before any of this session's changes -- not a
// new gap introduced by, or specific to, the left-to-right refinement
// added elsewhere in this file.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

bool take (bool) conveyor;

void f (int x) conveyor
{
  if (take (x < 100000 && x++ < 2048)) {} // { dg-error "increment of .x. not provably free of overflow" }
}

int main () { f (1); return 0; }
