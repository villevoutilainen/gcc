// D4324/P2680 item 8, Increment W2: member access through an unproven
// pointer ('p->field', a COMPONENT_REF over the same INDIRECT_REF
// shape as a plain '*p') is caught the same way as a bare dereference.
// The implicit '->' arrives as a compiler-synthesized INDIRECT_REF with
// no source location of its own (unlike an explicit '*p'); the error
// takes the statement's.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct thing { int count; };

int f (thing *p) conveyor
{
  return p->count; // { dg-error "pointer dereference of .*not provably valid" }
}

int main () { return 0; }
