// D4324: lambda-contract-captures-run.C with control objects, where the
// conditions are outlined into predicate functions: a capture proxy is out
// of scope there, and is replaced by the closure member it stands for.
// { dg-do run { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-evaluation-semantic=enforce" }

#include <contracts>

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
