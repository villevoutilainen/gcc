// D4324: a never-proven predicate's claim is trusted, but evaluating it
// must still be valid: a dereference in it ('p->v') needs its pointer
// established, as in any conveyor predicate.  is_object_address's
// argument is never evaluated ('&*this' included), a conjunct's trusted
// claim establishes the pointer for the conjuncts after it, and the
// predicate's arithmetic stays exempt from item 8.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=ast" }

#include <contracts>
using namespace std::contracts;

struct D { int v; };

int a (D *p) pre<never_proven_conveyor_v> (p->v > 0) { return 0; } // { dg-error "pointer dereference of 'p' not provably valid" }
int b (D *p) pre<never_proven_conveyor_v> (std::is_object_address (p) && p->v > 0) { return 0; }
int c (int x) pre<never_proven_conveyor_v> (x + 1 > 0) { return 0; }

struct S
{
  int v;
  int f () pre<never_proven_conveyor_v> (std::is_object_address (&*this)) { return 0; }
  int g () pre<never_proven_conveyor_v> (std::is_object_address (__builtin_addressof (*this))) { return 0; }
};

int d (D *p)
{
  contract_assert<never_proven_conveyor_v> (p->v > 0); // { dg-error "pointer dereference of 'p' not provably valid" }
  return 0;
}

int e (D *p)
{
  contract_assert<never_proven_conveyor_v> (std::is_object_address (p) && p->v > 0);
  return p->v;
}

int h (D *p, S &s, int x)
{ return a (p) + b (p) + c (x) + s.f () + s.g () + d (p) + e (p); }
