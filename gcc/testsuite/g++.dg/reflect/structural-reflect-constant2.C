// { dg-do compile { target c++26 } }
// { dg-additional-options "-freflection" }
// A class that deletes 'std::meta::info reflect_constant () const'
// (P4340) opts out of being structural, whatever its members.

#include <meta>

// Structural as it stands...
struct Plain
{
  int x;
};
static_assert (std::meta::is_structural_type (^^Plain));

// ...but not once it deletes reflect_constant.
struct OptOut
{
  int x;
  consteval std::meta::info reflect_constant () const = delete;
};
static_assert (!std::meta::is_structural_type (^^OptOut));

template <OptOut V> struct H {}; // { dg-error "not a valid type for a template non-type parameter" }

// With a trailing return type.
struct OptOut2
{
  int x;
  consteval auto reflect_constant () const -> std::meta::info = delete;
};
static_assert (!std::meta::is_structural_type (^^OptOut2));

// A class template, for each instantiation.
template <typename T>
struct Box
{
  T value;
  consteval std::meta::info reflect_constant () const = delete;
};
static_assert (!std::meta::is_structural_type (^^Box<int>));

// A base that opts out takes its derived classes with it.
struct Derived : OptOut
{
  int y;
};
static_assert (!std::meta::is_structural_type (^^Derived));

// Opting out of a class declared structural makes it ill-formed.
struct Both structural // { dg-error "marked .structural. but is not a structural type" }
{
  int x;
  consteval std::meta::info reflect_constant () const = delete;
};

// Any other reflect_constant is just a deleted function.
struct Other
{
  int x;
  int reflect_constant () const = delete;
  consteval std::meta::info reflect_constant (int) const = delete;
};
static_assert (std::meta::is_structural_type (^^Other));

// The defaulted form still opts in.
struct OptIn
{
private:
  int x;
public:
  consteval std::meta::info reflect_constant () const = default;
};
static_assert (std::meta::is_structural_type (^^OptIn));

// It has to be consteval to opt out, and a function returning
// std::meta::info has to be consteval anyway.
struct NotConsteval
{
  int x;
  std::meta::info reflect_constant () const = delete; // { dg-error "must be declared .consteval." }
};
static_assert (std::meta::is_structural_type (^^NotConsteval));
