// D4324, Increment U (defaulted-special-member half): a user-provided
// (non-defaulted) special member is never auto-inferred conveyor,
// regardless of its body's shape -- the inference machinery
// (synthesized_method_walk/implicitly_declare_fn) is only ever
// consulted for implicit or explicitly-defaulted special members, never
// for a hand-written one. Sanity baseline: ordinary hand-written
// special members, with no 'conveyor' keyword anywhere, keep compiling
// and running exactly as they always did.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct S
{
  int v;
  S () : v (0) {}
  S (int x) : v (x) {}
};

int main ()
{
  S a;
  S b (5);
  return a.v + b.v - 5;
}
