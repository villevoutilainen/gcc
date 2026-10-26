// A lambda's pre/postconditions see its captures as its body does: 'this'
// is the captured pointer (it was the closure object), a captured variable
// is the closure's copy (using one was an ICE), also in a class template
// (whose call operator's contracts weren't substituted at all).
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-evaluation-semantic=enforce" }

struct C
{
  int n;
  C *self;
  int f ()
  {
    int k = 3;
    auto g = [this, k] ()
      pre (self == this)
      pre (k == 3)
      post (r: r == n + k)
      { return n + k; };
    return g ();
  }
};

template<class T>
struct D
{
  T n;
  D *self;
  T f ()
  {
    T k = 4;
    auto g = [this, k] ()
      pre (self == this)
      pre (k == 4)
      post (r: r == n + k)
      { return n + k; };
    return g ();
  }
};

int
main ()
{
  C c{1, nullptr};
  c.self = &c;
  D<int> d{2, nullptr};
  d.self = &d;
  return c.f () + d.f () == 10 ? 0 : 1;
}
