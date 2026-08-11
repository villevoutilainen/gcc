// D4324/P2680 item 8, Increment W2: member access through an unproven
// pointer ('p->field', a COMPONENT_REF over the same INDIRECT_REF
// shape as a plain '*p') is caught the same way as a bare dereference.
// The diagnostic lands on the closing brace, not the 'p->count' line
// itself: the implicit '->' arrives as a compiler-synthesized
// INDIRECT_REF with no source location of its own (unlike an explicit
// '*p'), so the error falls back to input_location, confirmed via
// direct testing to be the function's own closing brace at this
// pre-genericize pass timing -- a real, narrow, disclosed diagnostic-
// quality gap, not a correctness one. See .claude/plans/well-we-last-
// discussed-ethereal-duckling.md.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

struct thing { int count; };

int f (thing *p) conveyor
{
  return p->count;
} // { dg-error "pointer dereference of .*not provably valid" }

int main () { return 0; }
