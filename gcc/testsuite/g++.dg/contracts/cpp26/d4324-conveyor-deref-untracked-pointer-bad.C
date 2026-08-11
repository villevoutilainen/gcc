// D4324/P2680 item 8, Increment W2: dereferencing a raw pointer with no
// established provenance at all (no array-offset fact, no
// is_object_address proof) inside a conveyor function is unprovable UB
// and now a mandatory error -- previously silently permitted, since the
// array-bound rule only ever checked pointers already carrying a
// tracked array-offset fact. See .claude/plans/well-we-last-discussed-
// ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

int f (int *p) conveyor
{
  return *p; // { dg-error "pointer dereference of .*not provably valid" }
}

int main () { return 0; }
