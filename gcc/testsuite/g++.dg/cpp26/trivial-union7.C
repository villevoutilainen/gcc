// P3726R2 - Adjustments to Union Lifetime Rules
// An element of an array member of a union whose lifetime has ended is an
// inactive union subobject, like one whose lifetime never began: the value
// is a constant expression, and template-argument-equivalent to one in
// which that element's lifetime never began.
// { dg-do compile { target c++26 } }

#include <new>

struct S { int x, y; };
template<typename T, int N>
struct V {
  union { T a[N]; };
  int n;
  constexpr V () : n (0) { __builtin_start_lifetime (&a); }
  constexpr void push (T t) { ::new (&a[n++]) T (t); }
  constexpr void pop () { a[--n].~T (); }
};

consteval V<int, 4> fresh () { V<int, 4> v; v.push (1); v.push (2); return v; }
consteval V<int, 4> popped ()
{
  V<int, 4> v; v.push (1); v.push (2); v.push (3); v.pop (); return v;
}
consteval V<int, 4> middle ()
{
  // Destroy the middle element and move the last one down, as erase does.
  V<int, 4> v; v.push (1); v.push (9); v.push (2);
  using I = int; v.a[1].~I (); ::new (&v.a[1]) int (v.a[2]); v.pop (); return v;
}
consteval V<int, 4> refilled ()
{
  V<int, 4> v; v.push (1); v.push (7); v.pop (); v.push (2); return v;
}
consteval V<S, 3> popped_aggr ()
{
  V<S, 3> v; v.push ({1, 2}); v.push ({3, 4}); v.pop (); return v;
}

constexpr V<int, 4> g1 = popped ();
constexpr V<int, 4> g2 = middle ();
constexpr V<S, 3> g3 = popped_aggr ();
static_assert (g1.n == 2 && g1.a[0] == 1 && g1.a[1] == 2);
static_assert (g2.n == 2 && g2.a[0] == 1 && g2.a[1] == 2);
static_assert (g3.n == 1 && g3.a[0].x == 1 && g3.a[0].y == 2);

template<V<int, 4>> struct T {};
template<typename, typename> constexpr bool same = false;
template<typename X> constexpr bool same<X, X> = true;
static_assert (same<T<fresh ()>, T<popped ()>>);
static_assert (same<T<fresh ()>, T<middle ()>>);
static_assert (same<T<fresh ()>, T<refilled ()>>);
T<popped ()> t1;
T<middle ()> t2;

consteval bool
within ()
{
  V<int, 4> v; v.push (1); v.push (2); v.pop ();
  return __builtin_is_within_lifetime (&v.a[0])
	 && !__builtin_is_within_lifetime (&v.a[1]);
}
static_assert (within ());

consteval int
read_destroyed ()
{
  V<int, 4> v; v.push (1); v.push (2); v.pop ();
  return v.a[1];		// { dg-error "accessing uninitialized array element" }
}
constexpr int r1 = read_destroyed ();	// { dg-message "in 'constexpr' expansion" }

// An element whose lifetime never began stays outside its lifetime when
// the object is returned from a function.
consteval V<int, 4> make () { return V<int, 4> (); }
consteval bool
never_began ()
{
  V<int, 4> v = make ();
  return !__builtin_is_within_lifetime (&v.a[1]);
}
static_assert (never_began ());

consteval int
read_never_began ()
{
  V<int, 4> v = make ();
  return v.a[1];		// { dg-error "accessing uninitialized array element" }
}
constexpr int r2 = read_never_began ();	// { dg-message "in 'constexpr' expansion" }
