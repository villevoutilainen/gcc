// P4340R1, as a conversion to std::meta::template_object_info
// (-freflection-structural-newmetainfo-conversion): examples of a class
// whose template-argument function builds on its members', calling their
// conversions explicitly to find their template parameter objects, then
// transforming some members and leaving others out of the value.  What a
// template parameter of the class sees is the canonical object: a member
// left out reads as the canonical object has it, not as the argument did.
// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection -freflection-structural-newmetainfo-conversion" }

#include <meta>
#include <type_traits>

using std::meta::info;
using std::meta::template_object_info;
using std::meta::extract;
using std::meta::reflect_constant;
using std::meta::substitute;

template <auto> struct T { };

// A member's canonical value, as its own template-argument function finds
// it, whichever it is: user-provided, defaulted or implicit.
template <class M>
consteval const M &
canonical_of (const M &m)
{
  return extract<const M &> (info (m.operator template_object_info ()));
}

// --- A member that normalizes itself: an angle in degrees, mod 360.
struct Angle
{
  int deg;
  consteval explicit operator template_object_info () const;
};
template <int D> constexpr Angle angle_canonical = {D};
consteval Angle::operator template_object_info () const
{ return template_object_info (substitute (^^angle_canonical, {reflect_constant ((deg % 360 + 360) % 360)})); }

// --- A member with no conversion of its own: its implicit one.
struct Point { int x, y; };

static_assert (canonical_of (Angle{370}).deg == 10);
static_assert (canonical_of (Point{1, 2}).y == 2);

// 1. Leaving out a cache.  A shape holds a derived value, its area,
//    computed once; it's no part of the shape's identity.  The canonical
//    object recomputes it from what is.
struct Rect
{
  Point origin;
  int w, h;
  int area;		// derived: w * h
  consteval explicit operator template_object_info () const;
};
template <int X, int Y, int W, int H>
constexpr Rect rect_canonical = {{X, Y}, W, H, W * H};
consteval Rect::operator template_object_info () const
{
  const Point &o = canonical_of (origin);
  return template_object_info (substitute (^^rect_canonical,
					    {reflect_constant (o.x), reflect_constant (o.y),
					     reflect_constant (w), reflect_constant (h)}));
}
// A stale cache doesn't make a different template argument.
static_assert (std::is_same_v<T<Rect{{0, 0}, 2, 3, 6}>, T<Rect{{0, 0}, 2, 3, -1}>>);
static_assert (!std::is_same_v<T<Rect{{0, 0}, 2, 3, 6}>, T<Rect{{0, 1}, 2, 3, 6}>>);
template <Rect R> constexpr int area_of = R.area;
static_assert (area_of<Rect{{0, 0}, 2, 3, -1}> == 6);		// recomputed

// 2. Leaving out unused storage.  A fixed-capacity string's characters
//    past its length are leftovers; only [0, len) is its value.
struct Name
{
  char buf[8];
  int len;
  consteval explicit operator template_object_info () const;
};
template <char C0, char C1, char C2, int L>
constexpr Name name_canonical = {{C0, C1, C2}, L};
consteval Name::operator template_object_info () const
{
  return template_object_info (substitute (^^name_canonical,
					    {reflect_constant (len > 0 ? buf[0] : '\0'),
					     reflect_constant (len > 1 ? buf[1] : '\0'),
					     reflect_constant (len > 2 ? buf[2] : '\0'),
					     reflect_constant (len)}));
}
static_assert (std::is_same_v<T<Name{{'a', 'b', 'x', 'y'}, 2}>, T<Name{{'a', 'b'}, 2}>>);
static_assert (!std::is_same_v<T<Name{{'a', 'b'}, 2}>, T<Name{{'a', 'c'}, 2}>>);

// 3. Transforming one member and keeping another as its own conversion
//    has it, while leaving out diagnostic data: where the value was made.
struct Heading
{
  Angle bearing;	// its own normalization, asked for
  Point at;		// kept as it is
  int line;		// diagnostic: the source line it was written on
  consteval explicit operator template_object_info () const;
};
template <int D, int X, int Y>
constexpr Heading heading_canonical = {{D}, {X, Y}, 0};
consteval Heading::operator template_object_info () const
{
  const Angle &b = canonical_of (bearing);
  const Point &p = canonical_of (at);
  return template_object_info (substitute (^^heading_canonical,
					    {reflect_constant (b.deg),
					     reflect_constant (p.x), reflect_constant (p.y)}));
}
static_assert (std::is_same_v<T<Heading{{370}, {1, 2}, __LINE__}>,
			      T<Heading{{10}, {1, 2}, __LINE__}>>);
static_assert (!std::is_same_v<T<Heading{{10}, {1, 2}, 0}>,
			       T<Heading{{10}, {2, 1}, 0}>>);
template <Heading H> constexpr int bearing_of = H.bearing.deg;
template <Heading H> constexpr int line_of = H.line;
static_assert (bearing_of<Heading{{725}, {0, 0}, 7}> == 5);	// normalized
static_assert (line_of<Heading{{0}, {0, 0}, 7}> == 0);		// not kept

// 4. An aggregate of such classes, each normalized by its own function:
//    it needs none, and its subobjects are normalized as members are.
struct Route { Heading from, to; };
static_assert (std::is_same_v<T<Route{{{370}, {0, 0}, 1}, {{-10}, {1, 1}, 2}}>,
			      T<Route{{{10}, {0, 0}, 3}, {{350}, {1, 1}, 4}}>>);
// Its implicit conversion gives that object too.
static_assert (canonical_of (Route{{{370}, {0, 0}, 1}, {{0}, {1, 1}, 2}}).from.bearing.deg == 10);
