// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }
// Test the C++26 context-sensitive "structural" class-head keyword:
// a class/struct so marked opts its own direct bases/members out of
// the C++20 structural-type rule's public-only requirement, as long
// as each one's own type is itself structural (recursively).

#include <meta>

// A plain class with private data is not structural...
class NotStructural
{
  int x;
public:
  constexpr NotStructural (int v) : x (v) {}
};
static_assert (!std::meta::is_structural_type (^^NotStructural));

// ...but the same shape marked "structural" is.
class IsStructural structural
{
  int x;
public:
  constexpr IsStructural (int v) : x (v) {}
  constexpr int get () const { return x; }
};
static_assert (std::meta::is_structural_type (^^IsStructural));

template <IsStructural V>
constexpr int use () { return V.get (); }
static_assert (use <IsStructural (42)> () == 42);

// Works for "struct" too, not just "class".
struct AlsoStructural structural
{
private:
  int y;
public:
  constexpr AlsoStructural (int v) : y (v) {}
};
static_assert (std::meta::is_structural_type (^^AlsoStructural));

// Recursive: a structural class's own member must itself be
// structural -- either because it's public-only, or because it's
// ALSO marked structural, however deep.
class Inner structural
{
  int v;
public:
  constexpr Inner (int x) : v (x) {}
  constexpr int get () const { return v; }
};
class Outer structural
{
  Inner inner;
public:
  constexpr Outer (int x) : inner (x) {}
  constexpr int get () const { return inner.get (); }
};
static_assert (std::meta::is_structural_type (^^Outer));
template <Outer O>
constexpr int use_outer () { return O.get (); }
static_assert (use_outer <Outer (7)> () == 7);

// A structural-marked class whose own member is NOT itself structural
// must still be rejected -- the recursive check isn't bypassed by the
// outer class's own exemption.
struct PlainInner
{
  int v;
private:
  int w = 0;
public:
  constexpr PlainInner (int x) : v (x) {}
};
class Outer2 structural
{
  PlainInner inner;
public:
  constexpr Outer2 (int x) : inner (x) {}
};
static_assert (!std::meta::is_structural_type (^^Outer2));

// Templates: the keyword survives template instantiation.
template <typename T>
class Box structural
{
  T value;
public:
  constexpr Box (T v) : value (v) {}
  constexpr T get () const { return value; }
};
static_assert (std::meta::is_structural_type (^^Box<int>));
template <Box<int> B>
constexpr int use_box () { return B.get (); }
static_assert (use_box <Box<int> (99)> () == 99);

// Rejected on a union.
union U structural // { dg-error "structural. not permitted on a union" }
{
  int a;
  float b;
};

// Duplicate specifier diagnosed, same as "final".
class Dup structural structural // { dg-error "duplicate .structural. specifier" }
{
  int x;
};

// Pre-C++26, "structural" stays an ordinary identifier -- tested
// separately in structural-pre-cxx26.C (a different dialect target
// can't be mixed into this same c++26-only file).
