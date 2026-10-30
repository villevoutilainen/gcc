// D4324: a template whose contracts name a non-dependent conveyor control
// object (no 'conveyor' keyword) carries the implicit is_object_address
// preconditions on 'this' and its reference parameters, as a non-template
// does: callers must prove them, and the body may rely on them.  Their
// control object's traits didn't fold while the template was being
// parsed, so it read as not conveyor and nothing was synthesized.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-fcontracts -fcontract-control-objects -fcontract-analysis-engine=gimple" }

#include <contracts>

struct It
{
  int v;
  It &operator-- () pre<std::contracts::conveyor_assert_v> (std::is_object_address (this))
  { --v; return *this; }
};

// Only a never-proven post: the body may still rely on 'this'.
template <class T>
struct W
{
  T cur;
  W &dec () post<std::contracts::never_proven_conveyor_v> (r: std::is_object_address (&r))
  { --cur; return *this; }
};

template <class T>
struct A
{
  T cur;
  A &inc () pre<std::contracts::conveyor_assert_v> (cur >= 0) { return *this; }
};

template <class T>
void h (T &x) pre<std::contracts::conveyor_assert_v> (true) {}

template <class T>
struct G
{
  int cur;
  template <class U>
  G &inc (U) pre<std::contracts::conveyor_assert_v> (true) { return *this; }
};

void
bad (int &i, W<It> &w, A<int> &a, G<int> &g)
{
  w.dec ();	// { dg-error "cannot prove" }
  a.inc ();	// { dg-error "cannot prove" }
  h (i);	// { dg-error "cannot prove" }
  g.inc (1);	// { dg-error "cannot prove" }
}

void
good ()
{
  int i = 0;
  W<It> w{};
  A<int> a{};
  G<int> g{};
  w.dec ();
  a.inc ();
  h (i);
  g.inc (1);
}
