// D4324/P2680 item 8's overflow scan: the decrement mirror of
// d4324-conveyor-overflow-increment-bad.C -- a bare '--x;' statement
// (not inside any assignment/return/condition -- oa_walk_stmt's own
// DEFAULT case, the same place a for-loop's own increment-clause lands)
// on an unconstrained parameter, so x could be TYPE_MIN and decrement
// could underflow.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int use_decrement_bad (int x) conveyor
{
  --x; // { dg-error "decrement of .x. not provably free of overflow" }
  return x;
}

int main () { return use_decrement_bad (0); }
