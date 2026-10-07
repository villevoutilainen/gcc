// P3446R0/P4296R0 Invalidation profile: the same chained, un-named
// accessor hazard as d4324-profiles-invalidation-chained-receiver-
// bad.C, but with an entirely hand-rolled, non-std, non-template
// class pair -- confirms the fix (ip_use_decl's new anonymous-SSA-
// temporary fallback, plus ip_defines_var_p's matching SSA_NAME
// special case) isn't somehow specific to std::vector, but a general
// property of any non-const accessor call whose own return value
// flows straight into another non-const call's receiver argument with
// nothing named in between.
// { dg-do compile { target c++11 } }

[[profiles::enforce(std::invalidation)]];

struct Inner
{
  int dummy = 0;
  void push_back (int) { }
};

struct Outer
{
  Inner elems[4];
  Inner &at (int i) { return elems[i]; }
  void resize (int) { }
  int capacity () { return 4; }
};

void f ()
{
  Outer v;
  v.at (0).push_back ((v.resize (v.capacity () + 1), 42)); // { dg-error "potentially invalidated" }
}
