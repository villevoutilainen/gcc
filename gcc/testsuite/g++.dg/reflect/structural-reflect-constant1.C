// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }
// A class that defaults 'std::meta::info reflect_constant () const'
// (P4340) is structural exactly as if it were declared with the
// class-property-specifier "structural" (structural1.C): its bases and
// members needn't be public, but must have structural types, and
// template arguments of its type are equivalent when their members are.

#include <meta>

class NotStructural
{
  int x;
public:
  constexpr NotStructural (int v) : x (v) {}
};
static_assert (!std::meta::is_structural_type (^^NotStructural));

class S
{
  int x;
public:
  constexpr S (int v) : x (v) {}
  constexpr int get () const { return x; }
  consteval std::meta::info reflect_constant () const = default;
};
static_assert (std::meta::is_structural_type (^^S));

template <S V>
constexpr int use () { return V.get (); }
static_assert (use <S (42)> () == 42);

// Template-argument equivalence goes by the private members.
template <S V> struct Holder {};
static_assert (std::is_same_v<Holder<S (1)>, Holder<S (1)>>);
static_assert (!std::is_same_v<Holder<S (1)>, Holder<S (2)>>);

// A trailing return type, and no consteval, work as well.
struct T2
{
private:
  int y;
public:
  constexpr T2 (int v) : y (v) {}
  constexpr int get () const { return y; }
  consteval auto reflect_constant () const -> std::meta::info = default;
};
static_assert (std::meta::is_structural_type (^^T2));
template <T2 V> struct Holder2 {};
static_assert (!std::is_same_v<Holder2<T2 (1)>, Holder2<T2 (3)>>);

struct T3
{
private:
  int z;
public:
  constexpr T3 (int v) : z (v) {}
  std::meta::info reflect_constant () const = default;
};
static_assert (std::meta::is_structural_type (^^T3));

// Recursive, through members and bases.
class Inner
{
  int v;
public:
  constexpr Inner (int x) : v (x) {}
  constexpr int get () const { return v; }
  consteval std::meta::info reflect_constant () const = default;
};
class Outer : private Inner
{
  Inner inner;
public:
  constexpr Outer (int x, int y) : Inner (x), inner (y) {}
  constexpr int get () const { return Inner::get () + inner.get (); }
  consteval std::meta::info reflect_constant () const = default;
};
static_assert (std::meta::is_structural_type (^^Outer));
template <Outer O>
constexpr int use_outer () { return O.get (); }
static_assert (use_outer <Outer (3, 4)> () == 7);

// A class template: each instantiation is structural, and checked.
template <typename T>
class Box
{
  T value;
public:
  constexpr Box (T v) : value (v) {}
  constexpr T get () const { return value; }
  consteval std::meta::info reflect_constant () const = default;
};
static_assert (std::meta::is_structural_type (^^Box<int>));
template <Box<int> B> struct BoxHolder {};
static_assert (!std::is_same_v<BoxHolder<Box<int> (1)>,
			       BoxHolder<Box<int> (2)>>);

class Box2Member
{
  int v;
public:
  constexpr Box2Member (int x) : v (x) {}
};
template <typename T>
class Box2 // { dg-error "defaults \[^\n\]* but is not a structural type" }
{
  T value;
public:
  constexpr Box2 (T v) : value (v) {}
  consteval std::meta::info reflect_constant () const = default;
};
Box2<int> b2i (1);
Box2<Box2Member> b2m (Box2Member (1));	// { dg-message "required from here" }

// A union.
union U
{
  int a;
  float b;
public:
  constexpr U (int x) : a (x) {}
  consteval std::meta::info reflect_constant () const = default;
};
static_assert (std::meta::is_structural_type (^^U));
template <U V> struct UHolder {};
static_assert (std::is_same_v<UHolder<U (1)>, UHolder<U (1)>>);

// The other requirements of a structural type still apply.
class Bad // { dg-error "defaults \[^\n\]* but is not a structural type" }
{
  NotStructural n;
public:
  consteval std::meta::info reflect_constant () const = default;
};
class Mut // { dg-error "defaults \[^\n\]* but is not a structural type" }
{
  mutable int m;
public:
  consteval std::meta::info reflect_constant () const = default;
};

// Only 'std::meta::info reflect_constant () const' can be defaulted.
struct NonConst
{
  consteval std::meta::info reflect_constant () = default; // { dg-error "must be a .const. member function" }
};
struct WithParm
{
  consteval std::meta::info reflect_constant (int) const = default; // { dg-error "must be a .const. member function" }
};
struct WrongType
{
  int reflect_constant () const = default; // { dg-error "must be a .const. member function" }
};
struct OutOfClass
{
  consteval std::meta::info reflect_constant () const;
};
consteval std::meta::info OutOfClass::reflect_constant () const = default; // { dg-error "can only be defaulted in its class" }

// It is only a marker: calling it is ill-formed.
consteval std::meta::info call (const S &s)
{
  return s.reflect_constant (); // { dg-error "use of deleted function" }
}
