// D4324/P2680 item 8's overflow scan: the mirror-image conjunct order in
// a switch's own discriminant, still correctly rejected.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int x) conveyor
{
  switch (x++ < 2048 && x < 100000) // { dg-error "increment of .x. not provably free of overflow" }
    {
    case 0: return 0;
    default: return 1;
    }
}

int main () { return f (1); }
