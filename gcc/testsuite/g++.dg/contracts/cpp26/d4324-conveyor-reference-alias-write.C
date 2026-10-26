// D4324: a call writing through a reference R bound to the reference
// parameter C changes the object C refers to, not which object that is (a
// reference can't be reseated), so C's is_object_address fact holds after
// it -- the AST engine dropped it, unlike for the same call made on C
// directly.  A pointer whose own storage is written through an alias still
// loses its fact.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects" }

#include <contracts>

struct V
{
  int x;
  int get () const conveyor { return x; }
  void set (int v) { x = v; }
};

int f (V &c)
  pre<std::contracts::conveyor_assert_v> (std::is_object_address (&c))
{
  auto &&r = c;
  r.set (1);
  return c.get ();
}

int g (V &c)
  pre<std::contracts::conveyor_assert_v> (std::is_object_address (&c))
{
  auto &&r = c;
  for (int i = 0; i < 3; ++i)
    r.set (i);
  return c.get ();
}

void reseat (V **);

int h (V *p)
  pre<std::contracts::conveyor_assert_v> (std::is_object_address (p))
{
  V **pp = &p;
  reseat (pp);
  return p->get (); // { dg-error "cannot prove 'is_object_address'" }
}
