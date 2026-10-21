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
// is ill-formed -- the recursive check isn't bypassed by the outer
// class's own exemption.
struct PlainInner
{
  int v;
private:
  int w = 0;
public:
  constexpr PlainInner (int x) : v (x) {}
};
class Outer2 structural // { dg-error "marked .structural. but is not a structural type" }
{
  PlainInner inner;
public:
  constexpr Outer2 (int x) : inner (x) {}
};
static_assert (!std::meta::is_structural_type (^^Outer2));

// Inheritance: next_aggregate_field enumerates base-class subobjects
// (as FIELD_DECLs with DECL_FIELD_IS_BASE) uniformly with ordinary
// members, so a base is subject to the exact same rules -- both when
// it's PUBLICLY inherited but has its own private DATA, and when the
// base-class SUBOBJECT itself is private (private inheritance), a
// genuinely distinct case from either class having private members.
class BaseWithPrivateData structural
{
  int bx;
public:
  constexpr BaseWithPrivateData (int x) : bx (x) {}
  constexpr int get_bx () const { return bx; }
};

class PublicDerived structural : public BaseWithPrivateData
{
  int dy;
public:
  constexpr PublicDerived (int x, int y) : BaseWithPrivateData (x), dy (y) {}
  constexpr int get_dy () const { return dy; }
};
static_assert (std::meta::is_structural_type (^^PublicDerived));
template <PublicDerived D>
constexpr int use_public_derived () { return D.get_bx () + D.get_dy (); }
static_assert (use_public_derived <PublicDerived (3, 4)> () == 7);

class PrivateDerived structural : private BaseWithPrivateData
{
  int dy;
public:
  constexpr PrivateDerived (int x, int y) : BaseWithPrivateData (x), dy (y) {}
  constexpr int get_bx () const { return BaseWithPrivateData::get_bx (); }
  constexpr int get_dy () const { return dy; }
};
static_assert (std::meta::is_structural_type (^^PrivateDerived));
template <PrivateDerived D>
constexpr int use_private_derived () { return D.get_bx () + D.get_dy (); }
static_assert (use_private_derived <PrivateDerived (3, 4)> () == 7);

// Negative control: an unmarked base with private data makes a
// structural-marked derived class ill-formed -- the recursive check
// through the base's own type is unconditional, just like for members.
class UnmarkedBase
{
  int bx;
public:
  constexpr UnmarkedBase (int x) : bx (x) {}
};
class DerivedFromUnmarked structural : public UnmarkedBase // { dg-error "marked .structural. but is not a structural type" }
{
  int dy;
public:
  constexpr DerivedFromUnmarked (int x, int y) : UnmarkedBase (x), dy (y) {}
};
static_assert (!std::meta::is_structural_type (^^DerivedFromUnmarked));

// Negative control: marking the base must not "infect" an unmarked
// derived class that has its own new private member -- each class
// needs its own keyword.
class UnmarkedDerived : public BaseWithPrivateData
{
  int dy;
public:
  constexpr UnmarkedDerived (int x, int y) : BaseWithPrivateData (x), dy (y) {}
};
static_assert (!std::meta::is_structural_type (^^UnmarkedDerived));

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

// Every requirement other than public access still applies: a mutable
// member, or a non-literal class, makes the definition ill-formed.
class Mut structural // { dg-error "marked .structural. but is not a structural type" }
{
  mutable int m;
public:
  constexpr Mut () : m (0) {}
};
struct NonLit { NonLit (); ~NonLit (); };
class HasNonLit structural // { dg-error "marked .structural. but is not a structural type" }
{
  NonLit n;
};

// For a class template, each instantiation is checked.
template <typename T>
class Box2 structural // { dg-error "marked .structural. but is not a structural type" }
{
  T value;
public:
  constexpr Box2 (T v) : value (v) {}
};
Box2<int> b2i (1);
Box2<PlainInner> b2p (PlainInner (1));	// { dg-message "required from here" }

// Unions: a union with private members can be marked structural too, and
// template-argument-equivalence still goes by the active member.
union U structural
{
  int a;
  float b;
public:
  constexpr U (int x) : a (x) {}
  constexpr U (float x) : b (x) {}
  constexpr int get_a () const { return a; }
};
static_assert (std::meta::is_structural_type (^^U));
template <U V>
constexpr int use_union () { return V.get_a (); }
static_assert (use_union <U (5)> () == 5);
template <U V> struct UHolder {};
static_assert (!std::is_same_v<UHolder<U (1)>, UHolder<U (1.0f)>>);
static_assert (std::is_same_v<UHolder<U (1)>, UHolder<U (1)>>);

// ...and the other requirements still apply to it.
union BadU structural // { dg-error "marked .structural. but is not a structural type" }
{
  PlainInner p;
  int i;
};

// Duplicate specifier diagnosed, same as "final".
class Dup structural structural // { dg-error "duplicate .structural. specifier" }
{
  int x;
};

// Pre-C++26, "structural" stays an ordinary identifier -- tested
// separately in structural-pre-cxx26.C (a different dialect target
// can't be mixed into this same c++26-only file).
