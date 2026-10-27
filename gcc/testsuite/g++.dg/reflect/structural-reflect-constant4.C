// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }
// In a class template, a defaulted reflect_constant (P4340) is deleted
// where a specialization can't be structural, rather than being
// ill-formed: that specialization opts out as if it deleted it.

#include <meta>

class NotStructural
{
  int x;
public:
  constexpr NotStructural (int v) : x (v) {}
};

struct Mutable
{
  mutable int m;
};

template <typename T>
class Box
{
  T value;
public:
  constexpr Box (T v) : value (v) {}
  constexpr T get () const { return value; }
  consteval std::meta::info reflect_constant () const = default; // { dg-message "is deleted because .Box<NotStructural>. would not be a structural type" }
};
static_assert (std::meta::is_structural_type (^^Box<int>));
static_assert (!std::meta::is_structural_type (^^Box<NotStructural>));
static_assert (!std::meta::is_structural_type (^^Box<Mutable>));

template <Box<int> B> struct Holder {};
static_assert (!std::is_same_v<Holder<Box<int> (1)>, Holder<Box<int> (2)>>);

template <Box<NotStructural> B> struct BadHolder {}; // { dg-error "not a valid type for a template non-type parameter because it is not structural" }

// A tuple-like recursion through private bases: each level is
// structural where all its elements are.
template <typename... Ts> class Tup;
template <> class Tup<>
{
public:
  consteval std::meta::info reflect_constant () const = default;
};
template <typename T, typename... Ts>
class Tup<T, Ts...> : private Tup<Ts...>
{
  T head;
public:
  constexpr Tup (T h, Ts... t) : Tup<Ts...> (t...), head (h) {}
  constexpr T first () const { return head; }
  consteval std::meta::info reflect_constant () const = default;
};
static_assert (std::meta::is_structural_type (^^Tup<int, long, char>));
static_assert (!std::meta::is_structural_type (^^Tup<int, NotStructural>));
static_assert (!std::meta::is_structural_type (^^Tup<NotStructural, int>));
template <Tup<int, long> V> struct TupHolder {};
static_assert (std::is_same_v<TupHolder<Tup<int, long> (1, 2)>,
			      TupHolder<Tup<int, long> (1, 2)>>);
static_assert (!std::is_same_v<TupHolder<Tup<int, long> (1, 2)>,
			       TupHolder<Tup<int, long> (1, 3)>>);

// Its storage can be a union.
template <typename T>
class Opt
{
  union { char empty; T val; };
  bool engaged;
public:
  constexpr Opt () : empty (), engaged (false) {}
  constexpr Opt (T v) : val (v), engaged (true) {}
  consteval std::meta::info reflect_constant () const = default;
};
static_assert (std::meta::is_structural_type (^^Opt<int>));
static_assert (!std::meta::is_structural_type (^^Opt<NotStructural>));
template <Opt<int> V> struct OptHolder {};
static_assert (!std::is_same_v<OptHolder<Opt<int> {}>, OptHolder<Opt<int> {0}>>);

// Not in a class that isn't a template specialization: there it's
// still ill-formed.
class Plain // { dg-error "defaults \[^\n\]* but is not a structural type" }
{
  NotStructural n;
public:
  consteval std::meta::info reflect_constant () const = default;
};

// Nor in an explicit specialization, which isn't dependent.
template <typename T> class Spec {};
template <>
class Spec<NotStructural> // { dg-error "defaults \[^\n\]* but is not a structural type" }
{
  NotStructural n;
public:
  consteval std::meta::info reflect_constant () const = default;
};

// Deleted or not, it can't be called.
consteval std::meta::info call (Box<NotStructural> b)
{
  return b.reflect_constant (); // { dg-error "use of deleted function" }
}
